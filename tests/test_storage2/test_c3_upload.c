#define _POSIX_C_SOURCE 200809L

#include "api.h"
#include "net.h"
#include "transfer.h"
#include "commands_storage.h"

#include <assert.h>
#include <pthread.h>
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

static void *upload_server_thread(void *arg)
{
    UploadTask *task = (UploadTask *)arg;
    task->result = cmd_upload(task->sock, task->uid, task->gid,
                              task->name, task->size);
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

static void assert_file_matches_bytes(int gid, const char *name,
                                      const unsigned char *expected,
                                      size_t expected_size)
{
    char path[512];
    snprintf(path, sizeof(path), "storage/groups/%d/%s", gid, name);

    struct stat st;
    assert(lstat(path, &st) == 0);
    assert(S_ISREG(st.st_mode));
    assert((size_t)st.st_size == expected_size);

    FILE *fp = fopen(path, "rb");
    assert(fp != NULL);
    if (expected_size > 0) {
        unsigned char *actual = malloc(expected_size);
        assert(actual != NULL);
        assert(fread(actual, 1, expected_size, fp) == expected_size);
        assert(memcmp(actual, expected, expected_size) == 0);
        free(actual);
    }
    fclose(fp);
}

// 1. Pre-READY rejection checks: client must NOT receive READY on errors
static void test_pre_ready_rejections(int gid)
{
    int sv[2];
    make_socket_pair(sv);

    mock_groups_set_role(1, gid, 2);      // leader
    mock_groups_set_role(3, gid, 0);      // outsider
    mock_groups_set_role(1, 999999, -1);  // non-existent group

    // Unauthenticated
    assert(cmd_upload(sv[0], -1, gid, "file.bin", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|AUTH_REQUIRED\n");

    // Non-existent group
    assert(cmd_upload(sv[0], 1, 999999, "file.bin", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|NOT_FOUND\n");

    // Outsider
    assert(cmd_upload(sv[0], 3, gid, "file.bin", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");

    // Invalid filenames
    assert(cmd_upload(sv[0], 1, gid, "../escape.bin", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");
    assert(cmd_upload(sv[0], 1, gid, "sub/file.bin", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");
    assert(cmd_upload(sv[0], 1, gid, "bad.part", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");

    close(sv[0]);
    close(sv[1]);
}

// 2. Upload 0-byte empty file
static void test_upload_zero_bytes(int gid)
{
    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    UploadTask task = {sv[0], 2, gid, "empty.txt", 0, -99};
    pthread_t th;
    assert(pthread_create(&th, NULL, upload_server_thread, &task) == 0);

    // Client receives READY, sends 0 bytes, and immediately gets OK|UPLOAD
    assert_recv_line_eq(sv[1], "READY\n");
    assert_recv_line_eq(sv[1], "OK|UPLOAD\n");

    pthread_join(th, NULL);
    assert(task.result == 0);
    assert_file_matches_bytes(gid, "empty.txt", NULL, 0);

    close(sv[0]);
    close(sv[1]);
}

// 3. Upload 257-byte binary file containing NUL (0x00), LF (0x0A), CR (0x0D), 0xFF
static void test_upload_binary_257_bytes(int gid)
{
    unsigned char payload[257];
    for (int i = 0; i < 256; i++) {
        payload[i] = (unsigned char)i;
    }
    payload[256] = 0xFF;

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    UploadTask task = {sv[0], 2, gid, "binary_257.bin", sizeof(payload), -99};
    pthread_t th;
    assert(pthread_create(&th, NULL, upload_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "READY\n");
    assert(send_all(sv[1], payload, sizeof(payload)) == 0);
    assert_recv_line_eq(sv[1], "OK|UPLOAD\n");

    pthread_join(th, NULL);
    assert(task.result == 0);
    assert_file_matches_bytes(gid, "binary_257.bin", payload, sizeof(payload));

    // Attempt to re-upload the same name -> must be rejected BEFORE READY
    assert(cmd_upload(sv[0], 2, gid, "binary_257.bin", 10) == 0);
    assert_recv_line_eq(sv[1], "ERR|EXISTS\n");
    // Original file must remain intact (257 bytes)
    assert_file_matches_bytes(gid, "binary_257.bin", payload, sizeof(payload));

    close(sv[0]);
    close(sv[1]);
}

// 4. Upload multi-chunk file (65536 + 7 = 65543 bytes) with fragmented/partial writes
static void test_upload_multichunk_and_partial_socket(int gid)
{
    const size_t total_size = 65536 + 7; // 65543 bytes (> 1 chunk of 64 KiB)
    unsigned char *payload = malloc(total_size);
    assert(payload != NULL);
    for (size_t i = 0; i < total_size; i++) {
        payload[i] = (unsigned char)((i * 31 + 17) & 0xFF);
    }

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(1, gid, 2);

    UploadTask task = {sv[0], 1, gid, "multi_65543.dat", total_size, -99};
    pthread_t th;
    assert(pthread_create(&th, NULL, upload_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "READY\n");

    // Send in uneven small fragments (e.g., 1 byte, 13 bytes, 4099 bytes...)
    size_t sent = 0;
    size_t steps[] = {1, 13, 509, 4099, 16381, 32771, 11769};
    for (size_t idx = 0; idx < sizeof(steps) / sizeof(steps[0]); idx++) {
        assert(send_all(sv[1], payload + sent, steps[idx]) == 0);
        sent += steps[idx];
    }
    assert(sent == total_size);

    assert_recv_line_eq(sv[1], "OK|UPLOAD\n");

    pthread_join(th, NULL);
    assert(task.result == 0);
    assert_file_matches_bytes(gid, "multi_65543.dat", payload, total_size);

    // Ensure LIST_FILES on the same connection works without byte boundary shift
    assert(cmd_list_files(sv[0], 1, gid, ".") == 0);
    assert_recv_line_eq(sv[1], "OK|FILES|3\n");
    for (int i = 0; i < 3; i++) {
        char line[MAX_LINE];
        assert(recv_line(sv[1], line, sizeof(line)) > 0);
    }
    assert_recv_line_eq(sv[1], "END\n");

    free(payload);
    close(sv[0]);
    close(sv[1]);
}

static void cleanup_group(int gid)
{
    char path[512];
    const char *files[] = {"empty.txt", "binary_257.bin", "multi_65543.dat"};
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

    int gid = 500000 + (int)getpid();
    assert(storage_create_group(gid) == 0);

    test_pre_ready_rejections(gid);
    test_upload_zero_bytes(gid);
    test_upload_binary_257_bytes(gid);
    test_upload_multichunk_and_partial_socket(gid);

    cleanup_group(gid);
    puts("TRANSFER C3 (STREAM FILE UPLOAD) TEST PASS");
    return 0;
}
