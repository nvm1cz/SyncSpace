
#include "api.h"
#include "groups_ext.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

static int reply(int sock, const char *s)
{
    return send_all(sock, s, strlen(s));
}

static int result(int sock, int rc, const char *success)
{
    if (rc == 0) return reply(sock, success);
    if (rc == SS_ERR_EXISTS) return reply(sock, "ERR|EXISTS\n");
    if (rc == SS_ERR_FULL) return reply(sock, "ERR|FULL\n");
    if (rc == SS_ERR_IO) return reply(sock, "ERR|IO\n");
    return reply(sock, "ERR|BAD_REQUEST\n");
}

// Tao nhom
int cmd_create_group(int sock, int uid, const char *name)
{
    if (uid <= 0) return reply(sock, "ERR|AUTH_REQUIRED\n");

    int id = group_create(uid, name);
    if (id < 0) return result(sock, id, "");

    char buf[100];
    snprintf(buf, sizeof(buf), "OK|GROUP|%d\n", id);
    return reply(sock, buf);
}

// Xem cac nhom da tham gia
int cmd_list_groups(int sock, int uid)
{
    if (uid <= 0) return reply(sock, "ERR|AUTH_REQUIRED\n");

    GroupInfo groups[100];
    int n = group_list(uid, groups, 100);
    if (n < 0) return result(sock, n, "");

    char buf[128];
    snprintf(buf, sizeof(buf), "OK|GROUPS|%d\n", n);
    if (reply(sock, buf) < 0) return -1;

    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "GROUP|%d|%s|%d\n",
                 groups[i].id, groups[i].name, groups[i].role);
        if (reply(sock, buf) < 0) return -1;
    }
    return reply(sock, "END\n");
}

// Xin tham gia nhom
int cmd_request_join(int sock, int uid, int gid)
{
    if (uid <= 0) return reply(sock, "ERR|AUTH_REQUIRED\n");
    if (gid <= 0) return reply(sock, "ERR|BAD_REQUEST\n");
    if (group_role(uid, gid) < 0)
        return reply(sock, "ERR|NOT_FOUND\n");

    return result(sock, group_request_join(uid, gid),
                  "OK|REQUEST_JOIN\n");
}

// Truong nhom duyet yeu cau
int cmd_approve_join(int sock, int uid, int gid, int user_id)
{
    if (uid <= 0) return reply(sock, "ERR|AUTH_REQUIRED\n");
    if (gid <= 0 || user_id <= 0)
        return reply(sock, "ERR|BAD_REQUEST\n");

    int role = group_role(uid, gid);
    if (role < 0) return reply(sock, "ERR|NOT_FOUND\n");
    if (role != 2) return reply(sock, "ERR|FORBIDDEN\n");

    return result(sock, group_approve_join(uid, gid, user_id),
                  "OK|APPROVE_JOIN\n");
}

// Xem danh sach yeu cau cho duyet
int cmd_list_requests(int sock, int uid, int gid)
{
    if (uid <= 0) return reply(sock, "ERR|AUTH_REQUIRED\n");
    if (gid <= 0) return reply(sock, "ERR|BAD_REQUEST\n");

    int role = group_role(uid, gid);
    if (role < 0) return reply(sock, "ERR|NOT_FOUND\n");
    if (role != 2) return reply(sock, "ERR|FORBIDDEN\n");

    int users[1000];
    int n = group_list_requests(uid, gid, users, 1000);
    if (n < 0) return result(sock, n, "");

    char buf[80];
    snprintf(buf, sizeof(buf), "OK|REQUESTS|%d\n", n);
    if (reply(sock, buf) < 0) return -1;

    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "USER|%d\n", users[i]);
        if (reply(sock, buf) < 0) return -1;
    }
    return reply(sock, "END\n");
}

// Xem thanh vien cua nhom
int cmd_list_members(int sock, int uid, int gid)
{
    if (uid <= 0) return reply(sock, "ERR|AUTH_REQUIRED\n");
    if (gid <= 0) return reply(sock, "ERR|BAD_REQUEST\n");

    int role = group_role(uid, gid);
    if (role < 0) return reply(sock, "ERR|NOT_FOUND\n");
    if (role == 0) return reply(sock, "ERR|FORBIDDEN\n");

    int users[1001];
    int n = group_list_members(gid, users, 1001);
    if (n < 0) return result(sock, n, "");

    char buf[80];
    snprintf(buf, sizeof(buf), "OK|MEMBERS|%d\n", n);
    if (reply(sock, buf) < 0) return -1;

    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "USER|%d\n", users[i]);
        if (reply(sock, buf) < 0) return -1;
    }
    return reply(sock, "END\n");
}
