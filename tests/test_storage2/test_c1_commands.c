#define _POSIX_C_SOURCE 200809L

#include "api.h"
#include "net.h"
#include "commands_storage.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

extern void mock_groups_reset(void);
extern void mock_groups_set_role(int user_id, int group_id, int role);

static void make_socket_pair(int sv[2])
{
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    assert(rc == 0);
}

static void assert_recv_line_eq(int sock, const char *expected)
{
    char buf[MAX_LINE];
    int n = recv_line(sock, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf, expected) == 0);
}

static void test_access_control(int gid)
{
    int sv[2];

    // 1. Unauthenticated user (uid <= 0) -> ERR|AUTH_REQUIRED
    make_socket_pair(sv);
    assert(cmd_mkdir(sv[0], -1, gid, "unauth_dir") == 0);
    assert_recv_line_eq(sv[1], "ERR|AUTH_REQUIRED\n");
    assert(cmd_list_files(sv[0], 0, gid, ".") == 0);
    assert_recv_line_eq(sv[1], "ERR|AUTH_REQUIRED\n");
    close(sv[0]);
    close(sv[1]);

    // 2. Non-existent group (role == -1) -> ERR|NOT_FOUND
    make_socket_pair(sv);
    mock_groups_set_role(1, 999999, -1);
    assert(cmd_mkdir(sv[0], 1, 999999, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|NOT_FOUND\n");
    assert(cmd_list_files(sv[0], 1, 999999, ".") == 0);
    assert_recv_line_eq(sv[1], "ERR|NOT_FOUND\n");
    close(sv[0]);
    close(sv[1]);

    // 3. Outsider (role == 0) -> ERR|FORBIDDEN
    make_socket_pair(sv);
    mock_groups_set_role(3, gid, 0);
    assert(cmd_mkdir(sv[0], 3, gid, "hacked") == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");
    assert(cmd_list_files(sv[0], 3, gid, ".") == 0);
    assert_recv_line_eq(sv[1], "ERR|FORBIDDEN\n");
    close(sv[0]);
    close(sv[1]);
}

static void test_mkdir_and_list_files(int gid)
{
    int sv[2];
    make_socket_pair(sv);

    // uid=1 is leader (role=2), uid=2 is member (role=1)
    mock_groups_set_role(1, gid, 2);
    mock_groups_set_role(2, gid, 1);

    // Empty group listing initially
    assert(cmd_list_files(sv[0], 2, gid, ".") == 0);
    assert_recv_line_eq(sv[1], "OK|FILES|0\n");
    assert_recv_line_eq(sv[1], "END\n");

    // Member creates subfolder "docs" -> OK|MKDIR
    assert(cmd_mkdir(sv[0], 2, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "OK|MKDIR\n");

    // Duplicate subfolder -> ERR|EXISTS
    assert(cmd_mkdir(sv[0], 1, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|EXISTS\n");

    // Invalid folder names -> ERR|BAD_REQUEST
    assert(cmd_mkdir(sv[0], 1, gid, "../escape") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");
    assert(cmd_mkdir(sv[0], 1, gid, "bad/name") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");

    // Create a regular file inside the group root to verify ITEM|F vs ITEM|D
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "storage/groups/%d/notes.txt", gid);
    FILE *fp = fopen(file_path, "wb");
    assert(fp != NULL);
    const char *payload = "HelloSyncSpace"; // 14 bytes
    assert(fwrite(payload, 1, 14, fp) == 14);
    fclose(fp);

    // List files as member: expect 2 items (ITEM|D|docs|0 and ITEM|F|notes.txt|14)
    assert(cmd_list_files(sv[0], 2, gid, ".") == 0);
    assert_recv_line_eq(sv[1], "OK|FILES|2\n");

    int saw_docs = 0;
    int saw_notes = 0;
    for (int i = 0; i < 2; i++) {
        char line[MAX_LINE];
        int n = recv_line(sv[1], line, sizeof(line));
        assert(n > 0);
        if (strcmp(line, "ITEM|D|docs|0\n") == 0) {
            saw_docs = 1;
        } else if (strcmp(line, "ITEM|F|notes.txt|14\n") == 0) {
            saw_notes = 1;
        }
    }
    assert(saw_docs && saw_notes);
    assert_recv_line_eq(sv[1], "END\n");

    // Invalid relative path for LIST_FILES in Week 2 -> ERR|BAD_REQUEST
    assert(cmd_list_files(sv[0], 1, gid, "docs") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");
    assert(cmd_list_files(sv[0], 1, gid, "../") == 0);
    assert_recv_line_eq(sv[1], "ERR|BAD_REQUEST\n");

    close(sv[0]);
    close(sv[1]);

    // Cleanup test files
    unlink(file_path);
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "storage/groups/%d/docs", gid);
    rmdir(dir_path);
    snprintf(dir_path, sizeof(dir_path), "storage/groups/%d", gid);
    rmdir(dir_path);
}

int main(void)
{
    mock_groups_reset();

    int gid = 300000 + (int)getpid();
    assert(storage_create_group(gid) == 0);

    test_access_control(gid);
    test_mkdir_and_list_files(gid);

    puts("TRANSFER C1 (MKDIR & LIST_FILES HANDLERS) TEST PASS");
    return 0;
}
