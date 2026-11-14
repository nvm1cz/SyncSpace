#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


int storage_mkdir(int group_id, const char *rel)
{
    (void)group_id;
    (void)rel;
    return SS_ERR_INVALID;
}

int storage_list(int group_id, const char *rel, FileInfo *out, int capacity)
{
    (void)group_id;
    (void)rel;
    (void)out;
    (void)capacity;
    return SS_ERR_INVALID;
}
