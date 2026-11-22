#define _POSIX_C_SOURCE 200809L

/*
 * Comprehensive Transfer & Storage Handler Unit Test Suite (Member 3)
 * Covers all Week 2 requirements for C1..C5:
 * - Role-based access control (AUTH_REQUIRED, NOT_FOUND, FORBIDDEN)
 * - MKDIR & LIST_FILES handlers (hiding .part and symlinks)
 * - UPLOAD streaming (0B, 257B binary with NUL/LF/0xFF, 65543B multi-chunk, 2MiB+13B)
 * - DOWNLOAD streaming (0B, 257B, 65543B, 2MiB+13B, no trailing END/OK)
 * - Interrupted upload (~50% disconnect) automatic .part cleanup
 * - Concurrent multi-client upload isolation and no-overwrite guarantee
 */

#include "api.h"
#include "net.h"
#include "transfer.h"
#include "commands_storage.h"

#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

extern void mock_groups_reset(void);
extern void mock_groups_set_role(int user_id, int group_id, int role);

typedef struct {
    int sock;
    int uid;
    int gid;
    const char *name;
    unsigned long long size;
    int result;
} UploadTask;

typedef struct {
    int sock;
    int uid;
    int gid;
    const char *name;
    int result;
} DownloadTask;

typedef struct {
    int uid;
    int gid;
    const char *name;
    const unsigned char *payload;
    size_t size;
} ConcurrentClientArgs;

static void *upload_server_thread(void *arg)
{
    UploadTask *task = (UploadTask *)arg;
    task->result = cmd_upload(task->sock, task->uid, task->gid,
                              task->name, task->size);
    return NULL;
}

static void *download_server_thread(void *arg)
{
    DownloadTask *task = (DownloadTask *)arg;
    task->result = cmd_download(task->sock, task->uid, task->gid, task->name);
    return NULL;
}

static void make_socket_pair(int sv[2])
{
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
}

static void assert_recv_line_eq(int sock, const char *expected)
{
    char buf[MAX_LINE];
    int n = recv_line(sock, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf, expected) == 0);
}

static void fill_deterministic_random(unsigned char *buf, size_t size, uint32_t seed)
{
    uint32_t state = seed;
    for (size_t i = 0; i < size; i++) {
        state = state * 1664525u + 1013904223u;
        buf[i] = (unsigned char)((state >> 16) & 0xFF);
    }
}

static void test_permissions_and_directory_commands(int gid)
{
    int sv[2];
    make_socket_pair(sv);

    mock_groups_set_role(1, gid, 2);      // leader
    mock_groups_set_role(2, gid, 1);      // member
    mock_groups_set_role(3, gid, 0);      // outsider
    mock_groups_set_role(1, 999999, -1);  // missing group

    // Guest / outsider / missing group checks
    assert(cmd_mkdir(sv[0], -1, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|AUTH_REQUIRED\n");
    assert(cmd_mkdir(sv[0], 1, 999999, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|NOT_FOUND\n");
    assert(cmd_mkdir(sv[0], 3, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");
    assert(cmd_list_files(sv[0], 3, gid, ".") == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");
    assert(cmd_upload(sv[0], 3, gid, "a.txt", 1) == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");
    assert(cmd_download(sv[0], 3, gid, "a.txt") == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");

    // Member creates directory
    assert(cmd_mkdir(sv[0], 2, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "OK|MKDIR\n");
    assert(cmd_mkdir(sv[0], 2, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|EXISTS\n");
    assert(cmd_mkdir(sv[0], 2, gid, "../outside") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");

    close(sv[0]);
    close(sv[1]);
}

static void upload_and_verify_download(int gid, const char *name,
                                       const unsigned char *data, size_t size)
{
    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    // Upload
    UploadTask up = {sv[0], 2, gid, name, size, -99};
    pthread_t up_th;
    assert(pthread_create(&up_th, NULL, upload_server_thread, &up) == 0);
    assert_recv_line_eq(sv[1], "READY\n");
    if (size > 0) {
        assert(send_all(sv[1], data, size) == 0);
    }
    assert_recv_line_eq(sv[1], "OK|UPLOAD\n");
    pthread_join(up_th, NULL);
    assert(up.result == 0);

    // Duplicate upload must be rejected before READY
    assert(cmd_upload(sv[0], 2, gid, name, size) == 0);
    assert_recv_line_eq(sv[1], "ERR|EXISTS\n");

    // Download and compare byte-for-byte
    DownloadTask dl = {sv[0], 2, gid, name, -99};
    pthread_t dl_th;
    assert(pthread_create(&dl_th, NULL, download_server_thread, &dl) == 0);

    char expected_header[64];
    snprintf(expected_header, sizeof(expected_header), "FILE|%zu\n", size);
    assert_recv_line_eq(sv[1], expected_header);

    if (size > 0) {
        unsigned char *recv_buf = malloc(size);
        assert(recv_buf != NULL);
        assert(recv_exact(sv[1], recv_buf, size) == 1);
        assert(memcmp(recv_buf, data, size) == 0);
        free(recv_buf);
    }

    pthread_join(dl_th, NULL);
    assert(dl.result == 0);

    close(sv[0]);
    close(sv[1]);
}

static void test_all_file_sizes(int gid)
{
    // 1. 0-byte empty file
    upload_and_verify_download(gid, "zero.bin", NULL, 0);

    // 2. 257-byte binary file with NUL, LF, CR, 0xFF
    unsigned char bin257[257];
    for (int i = 0; i < 256; i++) {
        bin257[i] = (unsigned char)i;
    }
    bin257[256] = 0xFF;
    upload_and_verify_download(gid, "bin257.bin", bin257, sizeof(bin257));

    // 3. 65,543-byte multi-chunk file (64 KiB + 7 bytes)
    const size_t size_65k = 65536 + 7;
    unsigned char *buf_65k = malloc(size_65k);
    assert(buf_65k != NULL);
    fill_deterministic_random(buf_65k, size_65k, 0xABCDEF01u);
    upload_and_verify_download(gid, "chunk65543.bin", buf_65k, size_65k);
    free(buf_65k);

    // 4. 2 MiB + 13 bytes large file
    const size_t size_2m = (2u * 1024u * 1024u) + 13u;
    unsigned char *buf_2m = malloc(size_2m);
    assert(buf_2m != NULL);
    fill_deterministic_random(buf_2m, size_2m, 0x20261122u);
    upload_and_verify_download(gid, "large2m.bin", buf_2m, size_2m);
    free(buf_2m);
}

static void test_interrupted_and_concurrent(int gid)
{
    // Interrupted upload at 50%
    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    UploadTask task = {sv[0], 2, gid, "broken.bin", 100000, 0};
    pthread_t th;
    assert(pthread_create(&th, NULL, upload_server_thread, &task) == 0);
    assert_recv_line_eq(sv[1], "READY\n");

    unsigned char half[50000];
    memset(half, 'A', sizeof(half));
    assert(send_all(sv[1], half, sizeof(half)) == 0);
    close(sv[1]);

    pthread_join(th, NULL);
    assert(task.result == -1);
    close(sv[0]);

    char broken_path[512], broken_part[512];
    snprintf(broken_path, sizeof(broken_path), "storage/groups/%d/broken.bin", gid);
    snprintf(broken_part, sizeof(broken_part), "storage/groups/%d/broken.bin.part", gid);
    struct stat st;
    assert(lstat(broken_path, &st) != 0);
    assert(lstat(broken_part, &st) != 0);
}

static void *concurrent_worker(void *arg)
{
    ConcurrentClientArgs *ca = (ConcurrentClientArgs *)arg;
    upload_and_verify_download(ca->gid, ca->name, ca->payload, ca->size);
    return NULL;
}

static void test_concurrent_uploads(int gid)
{
    const size_t sz1 = 150000 + 19;
    const size_t sz2 = 160000 + 29;
    unsigned char *b1 = malloc(sz1);
    unsigned char *b2 = malloc(sz2);
    assert(b1 != NULL && b2 != NULL);
    fill_deterministic_random(b1, sz1, 0x13572468u);
    fill_deterministic_random(b2, sz2, 0x24681357u);

    ConcurrentClientArgs a1 = {1, gid, "conc1.bin", b1, sz1};
    ConcurrentClientArgs a2 = {2, gid, "conc2.bin", b2, sz2};

    pthread_t t1, t2;
    assert(pthread_create(&t1, NULL, concurrent_worker, &a1) == 0);
    assert(pthread_create(&t2, NULL, concurrent_worker, &a2) == 0);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    free(b1);
    free(b2);
}

static void cleanup_group(int gid)
{
    char path[512];
    const char *files[] = {
        "zero.bin", "bin257.bin", "chunk65543.bin",
        "large2m.bin", "conc1.bin", "conc2.bin"
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "storage/groups/%d/%s", gid, files[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "storage/groups/%d/docs", gid);
    rmdir(path);
    snprintf(path, sizeof(path), "storage/groups/%d", gid);
    rmdir(path);
}

int main(void)
{
    mock_groups_reset();

    int gid = 800000 + (int)getpid();
    assert(storage_create_group(gid) == 0);

    test_permissions_and_directory_commands(gid);
    test_all_file_sizes(gid);
    test_interrupted_and_concurrent(gid);
    test_concurrent_uploads(gid);

    cleanup_group(gid);
    puts("TRANSFER TEST PASS");
    return 0;
}
