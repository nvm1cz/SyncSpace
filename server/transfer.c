#define _POSIX_C_SOURCE 200809L

#include "transfer.h"
#include "api.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define GROUPS_DIR "storage/groups"

static int ends_with_part(const char *name, size_t len)
{
    return (len >= 5 && strcmp(name + len - 5, ".part") == 0);
}

int transfer_validate_filename(const char *name)
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
    if (ends_with_part(name, len)) {
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

int transfer_build_paths(int group_id, const char *name,
                         char *dest_path, size_t dest_cap,
                         char *part_path, size_t part_cap)
{
    if (group_id <= 0 || !transfer_validate_filename(name)) {
        return SS_ERR_INVALID;
    }

    if (dest_path != NULL) {
        int dlen = snprintf(dest_path, dest_cap, "%s/%d/%s",
                            GROUPS_DIR, group_id, name);
        if (dlen < 0 || (size_t)dlen >= dest_cap) {
            return SS_ERR_INVALID;
        }
    }

    if (part_path != NULL) {
        int plen = snprintf(part_path, part_cap, "%s/%d/%s.part",
                            GROUPS_DIR, group_id, name);
        if (plen < 0 || (size_t)plen >= part_cap) {
            return SS_ERR_INVALID;
        }
    }

    return 0;
}

int transfer_open_temp(int group_id, const char *name,
                       int *fd_out,
                       char *dest_path, size_t dest_cap,
                       char *part_path, size_t part_cap)
{
    if (fd_out == NULL || dest_path == NULL || part_path == NULL) {
        return SS_ERR_INVALID;
    }
    *fd_out = -1;

    int rc = transfer_build_paths(group_id, name,
                                  dest_path, dest_cap,
                                  part_path, part_cap);
    if (rc != 0) {
        return rc;
    }

    char group_dir[512];
    int glen = snprintf(group_dir, sizeof(group_dir), "%s/%d",
                        GROUPS_DIR, group_id);
    if (glen < 0 || (size_t)glen >= sizeof(group_dir)) {
        return SS_ERR_INVALID;
    }

    struct stat st;
    if (lstat(group_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return SS_ERR_IO;
    }

    // Refuse overwriting any existing file, directory, or symlink
    if (lstat(dest_path, &st) == 0) {
        return SS_ERR_EXISTS;
    }
    if (errno != ENOENT) {
        return SS_ERR_IO;
    }

    int flags = O_CREAT | O_EXCL | O_WRONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(part_path, flags, 0644);
    if (fd < 0) {
        if (errno == EEXIST) {
            return SS_ERR_EXISTS;
        }
        return SS_ERR_IO;
    }

    *fd_out = fd;
    return 0;
}

int transfer_commit_temp(const char *part_path, const char *dest_path)
{
    if (part_path == NULL || dest_path == NULL) {
        return SS_ERR_INVALID;
    }

    struct stat st;
    if (lstat(part_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        unlink(part_path);
        return SS_ERR_IO;
    }

    if (lstat(dest_path, &st) == 0) {
        unlink(part_path);
        return SS_ERR_EXISTS;
    }

    // link() atomically fails with EEXIST if dest_path already exists
    if (link(part_path, dest_path) != 0) {
        int saved_errno = errno;
        unlink(part_path);
        if (saved_errno == EEXIST) {
            return SS_ERR_EXISTS;
        }
        return SS_ERR_IO;
    }

    unlink(part_path);
    return 0;
}

void transfer_abort_temp(int fd, const char *part_path)
{
    if (fd >= 0) {
        close(fd);
    }
    if (part_path != NULL && part_path[0] != '\0') {
        unlink(part_path);
    }
}

int transfer_open_download(int group_id, const char *name,
                           int *fd_out, unsigned long long *size_out)
{
    if (fd_out == NULL || size_out == NULL) {
        return SS_ERR_INVALID;
    }
    *fd_out = -1;
    *size_out = 0;

    char dest_path[1024];
    int rc = transfer_build_paths(group_id, name,
                                  dest_path, sizeof(dest_path),
                                  NULL, 0);
    if (rc != 0) {
        return rc;
    }

    struct stat lst;
    if (lstat(dest_path, &lst) != 0) {
        return SS_ERR_IO;
    }
    if (S_ISLNK(lst.st_mode) || !S_ISREG(lst.st_mode)) {
        return SS_ERR_INVALID;
    }

    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(dest_path, flags);
    if (fd < 0) {
        return SS_ERR_IO;
    }

    struct stat fst;
    if (fstat(fd, &fst) != 0 || !S_ISREG(fst.st_mode) || fst.st_size < 0) {
        close(fd);
        return SS_ERR_INVALID;
    }

    *fd_out = fd;
    *size_out = (unsigned long long)fst.st_size;
    return 0;
}

