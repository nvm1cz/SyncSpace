#define _POSIX_C_SOURCE 200809L

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

// Deterministic pseudo-random buffer generator + FNV-1a 64-bit hash verification
static void fill_deterministic_random(unsigned char *buf, size_t size, uint32_t seed)
{
    uint32_t state = seed;
    for (size_t i = 0; i < size; i++) {
        state = state * 1664525u + 1013904223u;
        buf[i] = (unsigned char)((state >> 16) & 0xFF);
    }
}

static uint64_t fnv1a_hash64(const unsigned char *buf, size_t size)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++) {
        hash ^= (uint64_t)buf[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void assert_no_part_files_remain(int gid)
{
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "storage/groups/%d", gid);
    DIR *dir = opendir(dir_path);
    assert(dir != NULL);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len >= 5) {
            assert(strcmp(entry->d_name + len - 5, ".part") != 0);
        }
    }
    closedir(dir);
}

// 1. Large file test: 2 MiB + 13 bytes (2,097,165 bytes) upload & download
static void test_large_file_2mib_plus_13(int gid)
{
    const size_t large_size = (2u * 1024u * 1024u) + 13u; // 2,097,165 bytes
    unsigned char *original = malloc(large_size);
    assert(original != NULL);
    fill_deterministic_random(original, large_size, 0x20261122u);
    uint64_t expected_hash = fnv1a_hash64(original, large_size);

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(1, gid, 2);

    // A. Stream upload 2 MiB + 13 bytes
    UploadTask up_task = {sv[0], 1, gid, "large_2mib.bin", large_size, -99};
    pthread_t up_th;
    assert(pthread_create(&up_th, NULL, upload_server_thread, &up_task) == 0);

    assert_recv_line_eq(sv[1], "READY\n");
    assert(send_all(sv[1], original, large_size) == 0);
    assert_recv_line_eq(sv[1], "OK|UPLOAD\n");

    pthread_join(up_th, NULL);
    assert(up_task.result == 0);

    // B. Stream download 2 MiB + 13 bytes on the same connection
    DownloadTask down_task = {sv[0], 1, gid, "large_2mib.bin", -99};
    pthread_t down_th;
    assert(pthread_create(&down_th, NULL, download_server_thread, &down_task) == 0);

    assert_recv_line_eq(sv[1], "FILE|2097165\n");

    unsigned char *downloaded = malloc(large_size);
    assert(downloaded != NULL);
    assert(recv_exact(sv[1], downloaded, large_size) == 1);

    pthread_join(down_th, NULL);
    assert(down_task.result == 0);

    // Verify both hash and exact byte-for-byte comparison
    assert(fnv1a_hash64(downloaded, large_size) == expected_hash);
    assert(memcmp(downloaded, original, large_size) == 0);

    free(downloaded);
    free(original);
    close(sv[0]);
    close(sv[1]);
}

// 2. Interrupted upload (~50% disconnect): must return -1, leave NO target file and NO .part file
static void test_interrupted_upload_cleanup(int gid)
{
    const size_t declared_size = 200000; // 200 KB declared
    const size_t partial_size = 100000;  // Disconnect at exactly 50% (100 KB)

    unsigned char *partial_buf = malloc(partial_size);
    assert(partial_buf != NULL);
    fill_deterministic_random(partial_buf, partial_size, 0xCAFEBABEu);

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    UploadTask task = {sv[0], 2, gid, "interrupted.bin", declared_size, 0};
    pthread_t th;
    assert(pthread_create(&th, NULL, upload_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "READY\n");
    // Send only 50% of the promised bytes, then abruptly close client socket!
    assert(send_all(sv[1], partial_buf, partial_size) == 0);
    close(sv[1]);

    pthread_join(th, NULL);
    // Handler must return -1 so server knows to close connection
    assert(task.result == -1);
    close(sv[0]);

    // Verify neither destination file nor .part file exists on disk
    char dest_path[512], part_path[512];
    snprintf(dest_path, sizeof(dest_path), "storage/groups/%d/interrupted.bin", gid);
    snprintf(part_path, sizeof(part_path), "storage/groups/%d/interrupted.bin.part", gid);
    struct stat st;
    assert(lstat(dest_path, &st) != 0);
    assert(lstat(part_path, &st) != 0);
    assert_no_part_files_remain(gid);

    free(partial_buf);
}

// 3. Two concurrent clients uploading different large files simultaneously
static void *concurrent_upload_worker(void *arg)
{
    ConcurrentClientArgs *ca = (ConcurrentClientArgs *)arg;
    int sv[2];
    make_socket_pair(sv);

    UploadTask task = {sv[0], ca->uid, ca->gid, ca->name, ca->size, -99};
    pthread_t server_th;
    assert(pthread_create(&server_th, NULL, upload_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "READY\n");

    // Stream in 17 KB chunks to interleave both threads heavily
    size_t sent = 0;
    while (sent < ca->size) {
        size_t chunk = (ca->size - sent > 17408) ? 17408 : (ca->size - sent);
        assert(send_all(sv[1], ca->payload + sent, chunk) == 0);
        sent += chunk;
    }

    assert_recv_line_eq(sv[1], "OK|UPLOAD\n");
    pthread_join(server_th, NULL);
    assert(task.result == 0);

    close(sv[0]);
    close(sv[1]);
    return NULL;
}

static void test_two_concurrent_uploads(int gid)
{
    const size_t size_a = 300000 + 37; // ~300 KB
    const size_t size_b = 320000 + 53; // ~320 KB

    unsigned char *buf_a = malloc(size_a);
    unsigned char *buf_b = malloc(size_b);
    assert(buf_a != NULL && buf_b != NULL);

    fill_deterministic_random(buf_a, size_a, 0x11112222u);
    fill_deterministic_random(buf_b, size_b, 0x33334444u);

    mock_groups_set_role(1, gid, 2);
    mock_groups_set_role(2, gid, 1);

    ConcurrentClientArgs args_a = {1, gid, "client_a.dat", buf_a, size_a};
    ConcurrentClientArgs args_b = {2, gid, "client_b.dat", buf_b, size_b};

    pthread_t th_a, th_b;
    assert(pthread_create(&th_a, NULL, concurrent_upload_worker, &args_a) == 0);
    assert(pthread_create(&th_b, NULL, concurrent_upload_worker, &args_b) == 0);

    pthread_join(th_a, NULL);
    pthread_join(th_b, NULL);

    // Download and verify both files have zero byte corruption or cross-talk
    int sv[2];
    make_socket_pair(sv);

    // Check client_a.dat
    DownloadTask dl_a = {sv[0], 1, gid, "client_a.dat", -99};
    pthread_t dl_th_a;
    assert(pthread_create(&dl_th_a, NULL, download_server_thread, &dl_a) == 0);
    assert_recv_line_eq(sv[1], "FILE|300037\n");
    unsigned char *recv_a = malloc(size_a);
    assert(recv_a != NULL);
    assert(recv_exact(sv[1], recv_a, size_a) == 1);
    pthread_join(dl_th_a, NULL);
    assert(dl_a.result == 0);
    assert(memcmp(recv_a, buf_a, size_a) == 0);

    // Check client_b.dat
    DownloadTask dl_b = {sv[0], 2, gid, "client_b.dat", -99};
    pthread_t dl_th_b;
    assert(pthread_create(&dl_th_b, NULL, download_server_thread, &dl_b) == 0);
    assert_recv_line_eq(sv[1], "FILE|320053\n");
    unsigned char *recv_b = malloc(size_b);
    assert(recv_b != NULL);
    assert(recv_exact(sv[1], recv_b, size_b) == 1);
    pthread_join(dl_th_b, NULL);
    assert(dl_b.result == 0);
    assert(memcmp(recv_b, buf_b, size_b) == 0);

    assert_no_part_files_remain(gid);

    free(recv_a);
    free(recv_b);
    free(buf_a);
    free(buf_b);
    close(sv[0]);
    close(sv[1]);
}

static void cleanup_group(int gid)
{
    char path[512];
    const char *files[] = {"large_2mib.bin", "client_a.dat", "client_b.dat"};
    for (size_t i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "storage/groups/%d/%s", gid, files[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "storage/groups/%d", gid);
    rmdir(path);
}

int main(void)
{
    mock_groups_reset();

    int gid = 700000 + (int)getpid();
    assert(storage_create_group(gid) == 0);

    test_large_file_2mib_plus_13(gid);
    test_interrupted_upload_cleanup(gid);
    test_two_concurrent_uploads(gid);

    cleanup_group(gid);
    puts("TRANSFER C5 (STRESS, 2MiB+13B, INTERRUPTED & CONCURRENT) TEST PASS");
    return 0;
}
