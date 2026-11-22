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
    int result;
} DownloadTask;

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

static void write_group_file(int gid, const char *name,
                             const unsigned char *data, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "storage/groups/%d/%s", gid, name);
    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    if (size > 0) {
        assert(fwrite(data, 1, size, fp) == size);
    }
    fclose(fp);
}

// 1. Unauthorized / missing / invalid download rejection checks (no raw bytes sent)
static void test_download_error_cases(int gid)
{
    int sv[2];
    make_socket_pair(sv);

    mock_groups_set_role(1, gid, 2);      // leader
    mock_groups_set_role(2, gid, 1);      // member
    mock_groups_set_role(3, gid, 0);      // outsider
    mock_groups_set_role(1, 999999, -1);  // non-existent group

    // Unauthenticated
    assert(cmd_download(sv[0], -1, gid, "any.txt") == 0);
    assert_recv_line_eq(sv[1], "ERR|AUTH_REQUIRED\n");

    // Non-existent group
    assert(cmd_download(sv[0], 1, 999999, "any.txt") == 0);
    assert_recv_line_eq(sv[1], "ERR|NOT_FOUND\n");

    // Outsider
    assert(cmd_download(sv[0], 3, gid, "any.txt") == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");

    // Missing file
    assert(cmd_download(sv[0], 2, gid, "missing.bin") == 0);
    assert_recv_line_eq(sv[1], "ERR|NOT_FOUND\n");

    // Invalid filenames / path traversal / .part
    assert(cmd_download(sv[0], 2, gid, "../secret.txt") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");
    assert(cmd_download(sv[0], 2, gid, "sub/file.txt") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");
    assert(cmd_download(sv[0], 2, gid, "temp.part") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");

    // Directory cannot be downloaded as a file
    assert(storage_mkdir(gid, "folder_only") == 0);
    assert(cmd_download(sv[0], 2, gid, "folder_only") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");

    close(sv[0]);
    close(sv[1]);
}

// 2. Download 0-byte empty file + verify no extra END/OK is sent after raw bytes
static void test_download_zero_bytes(int gid)
{
    write_group_file(gid, "empty.dat", NULL, 0);

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    DownloadTask task = {sv[0], 2, gid, "empty.dat", -99};
    pthread_t th;
    assert(pthread_create(&th, NULL, download_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "FILE|0\n");

    pthread_join(th, NULL);
    assert(task.result == 0);

    // Next command on the same socket must immediately match without leftover END/OK
    assert(cmd_mkdir(sv[0], 2, gid, "after_zero") == 0);
    assert_recv_line_eq(sv[1], "OK|MKDIR\n");

    close(sv[0]);
    close(sv[1]);
}

// 3. Download 257-byte binary file containing NUL (0x00), LF (0x0A), CR (0x0D), 0xFF
static void test_download_binary_257_bytes(int gid)
{
    unsigned char expected[257];
    for (int i = 0; i < 256; i++) {
        expected[i] = (unsigned char)i;
    }
    expected[256] = 0xFF;
    write_group_file(gid, "binary_257.bin", expected, sizeof(expected));

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(2, gid, 1);

    DownloadTask task = {sv[0], 2, gid, "binary_257.bin", -99};
    pthread_t th;
    assert(pthread_create(&th, NULL, download_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "FILE|257\n");

    unsigned char received[257];
    memset(received, 0, sizeof(received));
    assert(recv_exact(sv[1], received, sizeof(received)) == 1);
    assert(memcmp(received, expected, sizeof(expected)) == 0);

    pthread_join(th, NULL);
    assert(task.result == 0);

    close(sv[0]);
    close(sv[1]);
}

// 4. Download multi-chunk file (65536 + 7 = 65543 bytes) and check stream boundary
static void test_download_multichunk_65543_bytes(int gid)
{
    const size_t total_size = 65536 + 7; // 65543 bytes (> 64 KiB buffer)
    unsigned char *expected = malloc(total_size);
    assert(expected != NULL);
    for (size_t i = 0; i < total_size; i++) {
        expected[i] = (unsigned char)((i * 73 + 19) & 0xFF);
    }
    write_group_file(gid, "large_65543.bin", expected, total_size);

    int sv[2];
    make_socket_pair(sv);
    mock_groups_set_role(1, gid, 2);

    DownloadTask task = {sv[0], 1, gid, "large_65543.bin", -99};
    pthread_t th;
    assert(pthread_create(&th, NULL, download_server_thread, &task) == 0);

    assert_recv_line_eq(sv[1], "FILE|65543\n");

    unsigned char *received = malloc(total_size);
    assert(received != NULL);
    assert(recv_exact(sv[1], received, total_size) == 1);
    assert(memcmp(received, expected, total_size) == 0);

    pthread_join(th, NULL);
    assert(task.result == 0);

    // Follow-up command on the same socket must not be shifted by any trailing byte
    assert(cmd_mkdir(sv[0], 1, gid, "after_large") == 0);
    assert_recv_line_eq(sv[1], "OK|MKDIR\n");

    free(received);
    free(expected);
    close(sv[0]);
    close(sv[1]);
}

static void cleanup_group(int gid)
{
    char path[512];
    const char *files[] = {"empty.dat", "binary_257.bin", "large_65543.bin"};
    for (size_t i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "storage/groups/%d/%s", gid, files[i]);
        unlink(path);
    }
    const char *dirs[] = {"folder_only", "after_zero", "after_large"};
    for (size_t i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "storage/groups/%d/%s", gid, dirs[i]);
        rmdir(path);
    }
    snprintf(path, sizeof(path), "storage/groups/%d", gid);
    rmdir(path);
}

int main(void)
{
    mock_groups_reset();

    int gid = 600000 + (int)getpid();
    assert(storage_create_group(gid) == 0);

    test_download_error_cases(gid);
    test_download_zero_bytes(gid);
    test_download_binary_257_bytes(gid);
    test_download_multichunk_65543_bytes(gid);

    cleanup_group(gid);
    puts("TRANSFER C4 (STREAM FILE DOWNLOAD) TEST PASS");
    return 0;
}
