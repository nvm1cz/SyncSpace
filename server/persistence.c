#define _POSIX_C_SOURCE 200809L
#include "persistence.h"

#include <sqlite3.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* SQLite writes are serialized across the detached client threads. */
static sqlite3 *database;
static pthread_mutex_t database_lock = PTHREAD_MUTEX_INITIALIZER;

static int exec_sql(const char *sql)
{
    char *message = NULL;
    int rc = sqlite3_exec(database, sql, NULL, NULL, &message);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SyncSpace SQLite: %s\n", message ? message : sqlite3_errmsg(database));
        sqlite3_free(message);
        return -1;
    }
    return 0;
}

static int step_pair(const char *sql, int first, int second)
{
    sqlite3_stmt *statement = NULL;
    int ok = -1;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_int(statement, 1, first) == SQLITE_OK &&
        sqlite3_bind_int(statement, 2, second) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE) ok = 0;
    if (ok != 0) fprintf(stderr, "SyncSpace SQLite write: %s\n", sqlite3_errmsg(database));
    sqlite3_finalize(statement);
    return ok;
}

static int insert_named(const char *sql, int id, int owner_id, const char *name)
{
    sqlite3_stmt *statement = NULL;
    int ok = -1;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_int(statement, 1, id) == SQLITE_OK &&
        sqlite3_bind_int(statement, 2, owner_id) == SQLITE_OK &&
        sqlite3_bind_text(statement, 3, name, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE) ok = 0;
    if (ok != 0) fprintf(stderr, "SyncSpace SQLite write: %s\n", sqlite3_errmsg(database));
    sqlite3_finalize(statement);
    return ok;
}

int persistence_add_account(int id, const char *username, const char *password)
{
    pthread_mutex_lock(&database_lock);
    sqlite3_stmt *statement = NULL;
    int ok = -1;
    if (sqlite3_prepare_v2(database,
        "INSERT INTO accounts(id,username,password) VALUES(?1,?2,?3)",
        -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_int(statement, 1, id) == SQLITE_OK &&
        sqlite3_bind_text(statement, 2, username, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_text(statement, 3, password, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE) ok = 0;
    if (ok != 0) fprintf(stderr, "SyncSpace SQLite account: %s\n", sqlite3_errmsg(database));
    sqlite3_finalize(statement);
    pthread_mutex_unlock(&database_lock);
    return ok;
}

/* Never adopt orphaned files if a crash interrupted group creation. */
int persistence_group_path_is_unused(int id)
{
    struct stat st;
    char path[128];
    snprintf(path, sizeof(path), "storage/groups/%d", id);
    if (lstat(path, &st) == 0) return -1;
    return errno == ENOENT ? 0 : -1;
}

int persistence_add_group(int id, int owner_id, const char *name)
{
    pthread_mutex_lock(&database_lock);
    int ok = insert_named("INSERT INTO groups(id,owner_id,name) VALUES(?1,?2,?3)",
                          id, owner_id, name);
    pthread_mutex_unlock(&database_lock);
    return ok;
}

int persistence_add_request(int group_id, int user_id)
{
    pthread_mutex_lock(&database_lock);
    int ok = step_pair("INSERT INTO join_requests(group_id,user_id) VALUES(?1,?2)",
                       group_id, user_id);
    pthread_mutex_unlock(&database_lock);
    return ok;
}

int persistence_approve_request(int group_id, int user_id)
{
    pthread_mutex_lock(&database_lock);
    int ok = exec_sql("BEGIN IMMEDIATE") == 0 &&
             step_pair("INSERT INTO members(group_id,user_id) VALUES(?1,?2)",
                       group_id, user_id) == 0 &&
             step_pair("DELETE FROM join_requests WHERE group_id=?1 AND user_id=?2",
                       group_id, user_id) == 0 &&
             exec_sql("COMMIT") == 0;
    if (!ok) exec_sql("ROLLBACK");
    pthread_mutex_unlock(&database_lock);
    return ok ? 0 : -1;
}

static int legacy_data_exists(void)
{
    DIR *directory = opendir("storage/groups");
    if (directory == NULL) return errno == ENOENT ? 0 : 1;
    struct dirent *item;
    int found = 0;
    while ((item = readdir(directory)) != NULL) {
        if (strcmp(item->d_name, ".") && strcmp(item->d_name, "..")) {
            found = 1;
            break;
        }
    }
    closedir(directory);
    return found;
}

typedef int (*restore_row)(sqlite3_stmt *statement);

static int restore_account(sqlite3_stmt *s)
{
    return auth_restore(sqlite3_column_int(s, 0),
                        (const char *)sqlite3_column_text(s, 1),
                        (const char *)sqlite3_column_text(s, 2));
}

static int restore_group(sqlite3_stmt *s)
{
    int id = sqlite3_column_int(s, 0);
    struct stat st;
    char path[128];
    snprintf(path, sizeof(path), "storage/groups/%d", id);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    return group_restore(id, sqlite3_column_int(s, 1),
                         (const char *)sqlite3_column_text(s, 2));
}

static int restore_member(sqlite3_stmt *s)
{
    return group_restore_member(sqlite3_column_int(s, 0),
                                sqlite3_column_int(s, 1));
}

static int restore_request(sqlite3_stmt *s)
{
    return group_restore_request(sqlite3_column_int(s, 0),
                                 sqlite3_column_int(s, 1));
}

static int restore_table(const char *query, restore_row callback)
{
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2(database, query, -1, &statement, NULL);
    if (rc != SQLITE_OK) return -1;
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        if (callback(statement) != 0) {
            sqlite3_finalize(statement);
            return -1;
        }
    }
    sqlite3_finalize(statement);
    return rc == SQLITE_DONE ? 0 : -1;
}

int persistence_init(void)
{
    struct stat st;
    if (stat("storage", &st) != 0) {
        if (errno != ENOENT || mkdir("storage", 0700) != 0) {
            perror("SyncSpace storage");
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fputs("SyncSpace: 'storage' is not a directory\n", stderr);
        return -1;
    }

    if (stat("storage/syncspace.db", &st) != 0 && errno == ENOENT &&
        legacy_data_exists()) {
        fputs("SyncSpace: legacy group files exist without SQLite metadata. "
              "Back them up and migrate them; refusing to reassign ownership.\n", stderr);
        return -1;
    }

    if (sqlite3_open_v2("storage/syncspace.db", &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL) != SQLITE_OK) {
        fprintf(stderr, "SyncSpace SQLite open: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        database = NULL;
        return -1;
    }
    sqlite3_busy_timeout(database, 5000);

    if (exec_sql("PRAGMA foreign_keys = ON") != 0 ||
        exec_sql("CREATE TABLE IF NOT EXISTS accounts ("
                 "id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, password TEXT NOT NULL)") != 0 ||
        exec_sql("CREATE TABLE IF NOT EXISTS groups ("
                 "id INTEGER PRIMARY KEY, owner_id INTEGER NOT NULL REFERENCES accounts(id),"
                 "name TEXT NOT NULL, UNIQUE(owner_id,name))") != 0 ||
        exec_sql("CREATE TABLE IF NOT EXISTS members ("
                 "group_id INTEGER NOT NULL REFERENCES groups(id),"
                 "user_id INTEGER NOT NULL REFERENCES accounts(id),"
                 "PRIMARY KEY(group_id,user_id))") != 0 ||
        exec_sql("CREATE TABLE IF NOT EXISTS join_requests ("
                 "group_id INTEGER NOT NULL REFERENCES groups(id),"
                 "user_id INTEGER NOT NULL REFERENCES accounts(id),"
                 "PRIMARY KEY(group_id,user_id))") != 0 ||
        restore_table("SELECT id,username,password FROM accounts ORDER BY id", restore_account) != 0 ||
        restore_table("SELECT id,owner_id,name FROM groups ORDER BY id", restore_group) != 0 ||
        restore_table("SELECT group_id,user_id FROM members ORDER BY rowid", restore_member) != 0 ||
        restore_table("SELECT group_id,user_id FROM join_requests ORDER BY rowid", restore_request) != 0) {
        fputs("SyncSpace: failed to restore consistent database and group files\n", stderr);
        sqlite3_close(database);
        database = NULL;
        return -1;
    }
    puts("SyncSpace: SQLite state restored from storage/syncspace.db");
    return 0;
}
