#define _POSIX_C_SOURCE 200809L

#include "api.h"
#include "transfer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_filename_validation(void)
{
    // Valid filenames (1..64 chars in [A-Za-z0-9._-])
    assert(transfer_validate_filename("report.pdf") == 1);
    assert(transfer_validate_filename("a") == 1);
    assert(transfer_validate_filename("archive_2026-11.tar.gz") == 1);

    char max_name[65];
    memset(max_name, 'x', 64);
    max_name[64] = '\0';
    assert(transfer_validate_filename(max_name) == 1);

    // Invalid filenames: NULL, empty, >64 chars, ".", "..", paths, pipes, spaces
    assert(transfer_validate_filename(NULL) == 0);
    assert(transfer_validate_filename("") == 0);

    char too_long[66];
    memset(too_long, 'x', 65);
    too_long[65] = '\0';
    assert(transfer_validate_filename(too_long) == 0);

    assert(transfer_validate_filename(".") == 0);
    assert(transfer_validate_filename("..") == 0);
    assert(transfer_validate_filename("../secret.txt") == 0);
    assert(transfer_validate_filename("sub/file.txt") == 0);
    assert(transfer_validate_filename("sub\\file.txt") == 0);
    assert(transfer_validate_filename("bad|name.txt") == 0);
    assert(transfer_validate_filename("bad name.txt") == 0);
    assert(transfer_validate_filename("line\nbreak.txt") == 0);

    // Reserved .part extension must be rejected for both upload & mkdir
    assert(transfer_validate_filename("upload.part") == 0);
    assert(transfer_validate_filename(".part") == 0);
}

static void test_temp_file_and_atomic_no_overwrite(int gid)
{
    int fd = -1;
    char dest_path[1024];
    char part_path[1024];

    // 1. Open temporary .part file exclusively
    assert(transfer_open_temp(gid, "data.bin", &fd,
                              dest_path, sizeof(dest_path),
                              part_path, sizeof(part_path)) == 0);
    assert(fd >= 0);

    // Concurrent upload of the same filename while .part exists must return SS_ERR_EXISTS
    int fd2 = -1;
    char dest2[1024], part2[1024];
    assert(transfer_open_temp(gid, "data.bin", &fd2,
                              dest2, sizeof(dest2),
                              part2, sizeof(part2)) == SS_ERR_EXISTS);
    assert(fd2 == -1);

    // Write 10 bytes to temporary file
    const char *msg = "0123456789";
    assert(write(fd, msg, 10) == 10);
    close(fd);

    // 2. While .part file exists on disk, storage_list MUST hide it
    FileInfo items[16];
    int n = storage_list(gid, ".", items, 16);
    assert(n == 0);

    // 3. Commit .part atomically to destination
    assert(transfer_commit_temp(part_path, dest_path) == 0);

    // Verify .part is removed and dest_path exists
    struct stat st;
    assert(lstat(part_path, &st) != 0);
    assert(lstat(dest_path, &st) == 0 && st.st_size == 10);

    // Now storage_list must report data.bin (size 10)
    n = storage_list(gid, ".", items, 16);
    assert(n == 1);
    assert(strcmp(items[0].name, "data.bin") == 0);
    assert(items[0].is_dir == 0);
    assert(items[0].size == 10);

    // 4. Attempting to open temp or commit over an existing file must fail (no-overwrite)
    assert(transfer_open_temp(gid, "data.bin", &fd,
                              dest_path, sizeof(dest_path),
                              part_path, sizeof(part_path)) == SS_ERR_EXISTS);

    // Even if someone manually creates another .part and tries to commit over data.bin:
    int fd_manual = -1;
    char dest_other[1024], part_other[1024];
    assert(transfer_open_temp(gid, "other.bin", &fd_manual,
                              dest_other, sizeof(dest_other),
                              part_other, sizeof(part_other)) == 0);
    assert(write(fd_manual, "OVERWRITE_ATTEMPT", 17) == 17);
    close(fd_manual);
    assert(transfer_commit_temp(part_other, dest_path) == SS_ERR_EXISTS);
    // part_other must be cleaned up, and original data.bin must still be 10 bytes!
    assert(lstat(part_other, &st) != 0);
    assert(lstat(dest_path, &st) == 0 && st.st_size == 10);

    // 5. Test aborting an incomplete upload cleans up .part
    assert(transfer_open_temp(gid, "aborted.bin", &fd,
                              dest_other, sizeof(dest_other),
                              part_other, sizeof(part_other)) == 0);
    assert(write(fd, "PARTIAL", 7) == 7);
    transfer_abort_temp(fd, part_other);
    assert(lstat(part_other, &st) != 0);
    assert(lstat(dest_other, &st) != 0);
}

static void test_symlink_and_directory_rejection(int gid)
{
    // Create a subfolder "subdir"
    assert(storage_mkdir(gid, "subdir") == 0);

    // transfer_open_download must reject a directory
    int fd = -1;
    unsigned long long size = 0;
    assert(transfer_open_download(gid, "subdir", &fd, &size) == SS_ERR_INVALID);
    assert(fd == -1);

    // Attempt to create a symlink inside group root pointing outside
    char link_path[512];
    snprintf(link_path, sizeof(link_path), "storage/groups/%d/evil_link.txt", gid);
    unlink(link_path);
    if (symlink("/etc/passwd", link_path) == 0) {
        // storage_list must ignore symlinks
        FileInfo items[16];
        int n = storage_list(gid, ".", items, 16);
        for (int i = 0; i < n; i++) {
            assert(strcmp(items[i].name, "evil_link.txt") != 0);
        }

        // transfer_open_download must reject symlinks
        assert(transfer_open_download(gid, "evil_link.txt", &fd, &size) == SS_ERR_INVALID);
        assert(fd == -1);

        // transfer_open_temp must refuse to overwrite a symlink of the same name
        char dest[1024], part[1024];
        assert(transfer_open_temp(gid, "evil_link.txt", &fd,
                                  dest, sizeof(dest),
                                  part, sizeof(part)) == SS_ERR_EXISTS);
        unlink(link_path);
    }

    // Valid regular file download open must succeed
    assert(transfer_open_download(gid, "data.bin", &fd, &size) == 0);
    assert(fd >= 0);
    assert(size == 10);
    close(fd);
}

static void cleanup_group(int gid)
{
    char path[512];
    snprintf(path, sizeof(path), "storage/groups/%d/data.bin", gid);
    unlink(path);
    snprintf(path, sizeof(path), "storage/groups/%d/subdir", gid);
    rmdir(path);
    snprintf(path, sizeof(path), "storage/groups/%d", gid);
    rmdir(path);
}

int main(void)
{
    int gid = 400000 + (int)getpid();
    assert(storage_create_group(gid) == 0);

    test_filename_validation();
    test_temp_file_and_atomic_no_overwrite(gid);
    test_symlink_and_directory_rejection(gid);

    cleanup_group(gid);
    puts("TRANSFER C2 (SAFE PATH, SYMLINK & TEMP .PART FILES) TEST PASS");
    return 0;
}
