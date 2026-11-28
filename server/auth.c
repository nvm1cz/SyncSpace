#include "api.h"

#include <pthread.h>
#include <string.h>

typedef struct {
    char username[32];
    char password[129];
} Account;

static Account accounts[100];
static int account_count;
static pthread_mutex_t account_lock = PTHREAD_MUTEX_INITIALIZER;

static int valid_credentials(const char *username, const char *password)
{
    const char *username_chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";

    if (username == NULL || password == NULL) return 0;
    size_t username_length = strlen(username);
    size_t password_length = strlen(password);
    if (username_length == 0 || username_length >= sizeof(accounts[0].username) ||
        password_length == 0 || password_length >= sizeof(accounts[0].password) ||
        strspn(username, username_chars) != username_length) {
        return 0;
    }

    for (const unsigned char *p = (const unsigned char *)password; *p; p++) {
        if (*p < ' ' || *p > '~' || *p == '|') return 0;
    }
    return 1;
}

/* Call with account_lock held. IDs are one-based. */
static int find_account(const char *username)
{
    for (int i = 0; i < account_count; i++) {
        if (strcmp(accounts[i].username, username) == 0) return i + 1;
    }
    return SS_ERR_INVALID;
}

int auth_register(const char *username, const char *password)
{
    if (!valid_credentials(username, password)) return SS_ERR_INVALID;

    pthread_mutex_lock(&account_lock);
    int result;
    if (find_account(username) > 0) {
        result = SS_ERR_EXISTS;
    } else if (account_count == (int)(sizeof(accounts) / sizeof(accounts[0]))) {
        result = SS_ERR_FULL;
    } else {
        strcpy(accounts[account_count].username, username);
        strcpy(accounts[account_count].password, password);
        result = ++account_count;
    }
    pthread_mutex_unlock(&account_lock);
    return result;
}

int auth_login(const char *username, const char *password)
{
    if (!valid_credentials(username, password)) return SS_ERR_INVALID;

    pthread_mutex_lock(&account_lock);
    int id = find_account(username);
    if (id > 0 && strcmp(accounts[id - 1].password, password) != 0) {
        id = SS_ERR_INVALID;
    }
    pthread_mutex_unlock(&account_lock);
    return id;
}
