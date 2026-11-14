#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define STORAGE_ROOT "storage"
#define GROUPS_DIR   "storage/groups"

// Create directory if it does not exist
static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : SS_ERR_IO;
    }

    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST) {
            return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : SS_ERR_IO;
        }
        return SS_ERR_IO;
    }
    return 0;
}

// Ensure parent directories storage/ and storage/groups/ exist
static int ensure_parent_dirs(void)
{
    if (ensure_dir(STORAGE_ROOT) != 0 || ensure_dir(GROUPS_DIR) != 0) {
        return SS_ERR_IO;
    }
    return 0;
}

// Validate folder/file name: 1-64 chars [A-Za-z0-9._-], reject . and ..
static int validate_name(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    size_t len = strlen(name);
    if (len < 1 || len > 64) {
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') {
            return 0;
        }
    }
    return 1;
}

// Create group root directory: storage/groups/<group_id>
int storage_create_group(int group_id)
{
    if (group_id <= 0) {
        return SS_ERR_INVALID;
    }

    if (ensure_parent_dirs() != 0) {
        return SS_ERR_IO;
    }

    char group_path[512];
    int len = snprintf(group_path, sizeof(group_path), "%s/%d", GROUPS_DIR, group_id);
    if (len < 0 || (size_t)len >= sizeof(group_path)) {
        return SS_ERR_INVALID;
    }

    struct stat st;
    if (stat(group_path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : SS_ERR_IO;
    }

    if (mkdir(group_path, 0755) != 0) {
        if (errno == EEXIST) {
            return (stat(group_path, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : SS_ERR_IO;
        }
        return SS_ERR_IO;
    }

    return 0;
}

// Create a direct subfolder under group root: storage/groups/<group_id>/<rel>
int storage_mkdir(int group_id, const char *rel)
{
    if (group_id <= 0 || rel == NULL) {
        return SS_ERR_INVALID;
    }

    if (!validate_name(rel)) {
        return SS_ERR_INVALID;
    }

    char group_path[512];
    snprintf(group_path, sizeof(group_path), "%s/%d", GROUPS_DIR, group_id);

    struct stat st;
    if (stat(group_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return SS_ERR_IO;
    }

    char path[1024];
    int len = snprintf(path, sizeof(path), "%s/%s", group_path, rel);
    if (len < 0 || (size_t)len >= sizeof(path)) {
        return SS_ERR_INVALID;
    }

    if (stat(path, &st) == 0) {
        return SS_ERR_EXISTS;
    }

    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST) {
            return SS_ERR_EXISTS;
        }
        return SS_ERR_IO;
    }

    return 0;
}

// List items in group root directory (rel must be "." in Week 1)
int storage_list(int group_id, const char *rel, FileInfo *out, int capacity)
{
    if (group_id <= 0 || rel == NULL || out == NULL || capacity <= 0) {
        return SS_ERR_INVALID;
    }

    if (strcmp(rel, ".") != 0) {
        return SS_ERR_INVALID;
    }

    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%d", GROUPS_DIR, group_id);

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return SS_ERR_INVALID;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (count >= capacity) {
            closedir(dir);
            return SS_ERR_FULL;
        }

        char item_path[1024];
        int len = snprintf(item_path, sizeof(item_path), "%s/%s", dir_path, entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(item_path)) {
            continue;
        }

        struct stat st;
        if (lstat(item_path, &st) != 0) {
            continue;
        }

        if (S_ISLNK(st.st_mode)) {
            continue;
        }

        if (strlen(entry->d_name) >= sizeof(out[count].name)) {
            continue;
        }

        snprintf(out[count].name, sizeof(out[count].name), "%.64s", entry->d_name);
        if (S_ISDIR(st.st_mode)) {
            out[count].is_dir = 1;
            out[count].size = 0;
        } else {
            out[count].is_dir = 0;
            out[count].size = (long long)st.st_size;
        }

        count++;
    }

    closedir(dir);
    return count;
}
