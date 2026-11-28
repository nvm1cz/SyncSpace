#include "api.h"
#include "commands_groups.h"
#include "net.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void check(int sock, const char *expected)
{
    char buf[256];
    assert(recv_line(sock, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, expected) == 0);
}

int main(void)
{
    int s[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, s) == 0);

    // 1. Tao nhom
    assert(cmd_create_group(s[0], 1, "TeamA") == 0);
    check(s[1], "OK|GROUP|1\n");

    // 2. Xin tham gia
    assert(cmd_request_join(s[0], 2, 1) == 0);
    check(s[1], "OK|REQUEST_JOIN\n");

    // 3. Xem yeu cau cho duyet
    assert(cmd_list_requests(s[0], 1, 1) == 0);
    check(s[1], "OK|REQUESTS|1\n");
    check(s[1], "USER|2\n");
    check(s[1], "END\n");

    // 4. Duyet thanh vien
    assert(cmd_approve_join(s[0], 1, 1, 2) == 0);
    check(s[1], "OK|APPROVE_JOIN\n");

    // 5. Xem danh sach thanh vien
    assert(cmd_list_members(s[0], 2, 1) == 0);
    check(s[1], "OK|MEMBERS|2\n");
    check(s[1], "USER|1\n");
    check(s[1], "USER|2\n");
    check(s[1], "END\n");

    // 6. Thanh vien xem nhom da tham gia
    assert(cmd_list_groups(s[0], 2) == 0);
    check(s[1], "OK|GROUPS|1\n");
    check(s[1], "GROUP|1|TeamA|1\n");
    check(s[1], "END\n");

    // 7. Kiem tra phan quyen
    assert(cmd_approve_join(s[0], 2, 1, 3) == 0);
    check(s[1], "ERR|FORBIDDEN\n");

    assert(cmd_list_members(s[0], 3, 1) == 0);
    check(s[1], "ERR|FORBIDDEN\n");

    close(s[0]);
    close(s[1]);

    printf("GROUP TCP COMMANDS TEST PASS\n");
    return 0;
}
