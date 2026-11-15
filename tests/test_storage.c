#include "api.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Minimal test required by Sprint 01 specification (Section 7.3)
static void test_basic(void)
{
    int gid = 100000 + (int)getpid();
    assert(storage_create_group(gid) == 0);
    assert(storage_mkdir(gid, "docs") == 0);
    assert(storage_mkdir(gid, "../outside") == -1);

    FileInfo items[100];
    int n = storage_list(gid, ".", items, 100);
    assert(n == 1);
    assert(items[0].is_dir == 1);
}

// Path safety, input validation, and security edge cases
static void test_path_safety(void)
{
    int gid = 100000 + (int)getpid();

    // Reject path traversal
    assert(storage_mkdir(gid, "../outside") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "../../etc") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "/abs") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "dir/../other") == SS_ERR_INVALID);

    // Reject special directory entries
    assert(storage_mkdir(gid, ".") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "..") == SS_ERR_INVALID);

    // Reject invalid characters: slash, backslash, pipe, space
    assert(storage_mkdir(gid, "a/b") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "a\\b") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "bad|name") == SS_ERR_INVALID);
    assert(storage_mkdir(gid, "bad name") == SS_ERR_INVALID);

    // Reject empty name and names longer than 64 characters
    assert(storage_mkdir(gid, "") == SS_ERR_INVALID);
    char long_name[70];
    memset(long_name, 'a', 65);
    long_name[65] = '\0';
    assert(storage_mkdir(gid, long_name) == SS_ERR_INVALID);

    // Reject invalid group_id
    assert(storage_create_group(0) == SS_ERR_INVALID);
    assert(storage_create_group(-1) == SS_ERR_INVALID);
    assert(storage_mkdir(0, "docs") == SS_ERR_INVALID);
    assert(storage_mkdir(-1, "docs") == SS_ERR_INVALID);

    // Idempotency and duplicate directory error
    assert(storage_create_group(gid) == 0);
    assert(storage_mkdir(gid, "docs") == SS_ERR_EXISTS);
}

// Listing, file vs directory attributes, and capacity checks
static void test_listing_and_capacity(void)
{
    int gid_empty = 200000 + (int)getpid();
    assert(storage_create_group(gid_empty) == 0);

    // List empty directory
    FileInfo items[10];
    assert(storage_list(gid_empty, ".", items, 10) == 0);

    int gid = 100000 + (int)getpid();
    assert(storage_mkdir(gid, "sub_dir-1.0") == 0);

    // Create a regular file to test size and is_dir distinction
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "storage/groups/%d/test.txt", gid);
    FILE *fp = fopen(file_path, "w");
    assert(fp != NULL);
    const char *content = "SyncSpaceStorage"; // 16 bytes
    fputs(content, fp);
    fclose(fp);

    // List gid: contains docs (dir), sub_dir-1.0 (dir), test.txt (file)
    int n = storage_list(gid, ".", items, 10);
    assert(n == 3);

    int found_docs = 0;
    int found_subdir = 0;
    int found_file = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i].name, "docs") == 0) {
            assert(items[i].is_dir == 1);
            assert(items[i].size == 0);
            found_docs = 1;
        } else if (strcmp(items[i].name, "sub_dir-1.0") == 0) {
            assert(items[i].is_dir == 1);
            assert(items[i].size == 0);
            found_subdir = 1;
        } else if (strcmp(items[i].name, "test.txt") == 0) {
            assert(items[i].is_dir == 0);
            assert(items[i].size == 16);
            found_file = 1;
        }
    }
    assert(found_docs && found_subdir && found_file);

    // Reject non-root rel in Week 1
    assert(storage_list(gid, "docs", items, 10) == SS_ERR_INVALID);
    assert(storage_list(gid, NULL, items, 10) == SS_ERR_INVALID);

    // Capacity overflow and invalid parameters
    assert(storage_list(gid, ".", items, 2) == SS_ERR_FULL); // 3 items > capacity 2
    assert(storage_list(gid, ".", items, 0) == SS_ERR_INVALID);
    assert(storage_list(gid, ".", NULL, 10) == SS_ERR_INVALID);
    assert(storage_list(0, ".", items, 10) == SS_ERR_INVALID);
}

int main(void)
{
    test_basic();
    test_path_safety();
    test_listing_and_capacity();

    puts("STORAGE TEST PASS");
    return 0;
}
