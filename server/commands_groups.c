#include "commands_groups.h"
#include "api.h"
#include "commands.h"
#include "groups_ext.h"
#include "net.h"

#include <stdio.h>

static int reply(int sock, const char *line)
{
    return send_all(sock, line, strlen(line));
}

static int group_error(int sock, int code)
{
    switch (code) {
        case SS_ERR_EXISTS: return reply(sock, "ERR|EXISTS\n");
        case SS_ERR_FULL: return reply(sock, "ERR|FULL\n");
        case SS_ERR_INVALID: return reply(sock, SS_BAD_REQUEST);
        default: return reply(sock, "ERR|IO\n");
    }
}

static const char *access_error(int uid, int gid, int required_role)
{
    if (uid <= 0) return SS_AUTH_REQUIRED;
    if (gid <= 0) return SS_BAD_REQUEST;

    int role = group_role(uid, gid);
    if (role < 0) return "ERR|NOT_FOUND\n";
    if (role < required_role) return "ERR|FORBIDDEN\n";
    return NULL;
}

int cmd_create_group(int sock, int uid, const char *name)
{
    if (uid <= 0) return reply(sock, SS_AUTH_REQUIRED);

    int id = group_create(uid, name);
    if (id < 0) return group_error(sock, id);
    char line[64];
    snprintf(line, sizeof(line), "OK|GROUP|%d\n", id);
    return reply(sock, line);
}

int cmd_list_groups(int sock, int uid)
{
    if (uid <= 0) return reply(sock, SS_AUTH_REQUIRED);

    GroupInfo groups[100];
    int count = group_list(uid, groups, (int)(sizeof(groups) / sizeof(groups[0])));
    if (count < 0) return group_error(sock, count);

    char line[MAX_LINE];
    snprintf(line, sizeof(line), "OK|GROUPS|%d\n", count);
    if (reply(sock, line) < 0) return -1;
    for (int i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "GROUP|%d|%s|%d\n",
                 groups[i].id, groups[i].name, groups[i].role);
        if (reply(sock, line) < 0) return -1;
    }
    return reply(sock, "END\n");
}

int cmd_request_join(int sock, int uid, int gid)
{
    const char *error = access_error(uid, gid, 0);
    if (error != NULL) return reply(sock, error);

    int result = group_request_join(uid, gid);
    if (result < 0) return group_error(sock, result);
    return reply(sock, "OK|REQUESTED\n");
}

int cmd_list_requests(int sock, int uid, int gid)
{
    const char *error = access_error(uid, gid, 2);
    if (error != NULL) return reply(sock, error);

    int ids[1000];
    int count = group_list_requests(uid, gid, ids, (int)(sizeof(ids) / sizeof(ids[0])));
    if (count < 0) return group_error(sock, count);

    char line[64];
    snprintf(line, sizeof(line), "OK|REQUESTS|%d\n", count);
    if (reply(sock, line) < 0) return -1;
    for (int i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "REQUEST|%d\n", ids[i]);
        if (reply(sock, line) < 0) return -1;
    }
    return reply(sock, "END\n");
}

int cmd_approve_join(int sock, int uid, int gid, int target_uid)
{
    const char *error = access_error(uid, gid, 2);
    if (error != NULL) return reply(sock, error);
    if (target_uid <= 0) return reply(sock, SS_BAD_REQUEST);

    int result = group_approve_join(uid, gid, target_uid);
    if (result == SS_ERR_INVALID) return reply(sock, "ERR|NOT_FOUND\n");
    if (result < 0) return group_error(sock, result);
    return reply(sock, "OK|APPROVED\n");
}

int cmd_list_members(int sock, int uid, int gid)
{
    const char *error = access_error(uid, gid, 1);
    if (error != NULL) return reply(sock, error);

    /* Groups stores up to 1000 joined members, plus the owner. */
    int ids[1001];
    int count = group_list_members(gid, ids, (int)(sizeof(ids) / sizeof(ids[0])));
    if (count < 0) return group_error(sock, count);

    char line[64];
    snprintf(line, sizeof(line), "OK|MEMBERS|%d\n", count);
    if (reply(sock, line) < 0) return -1;
    for (int i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "MEMBER|%d|%d\n", ids[i], group_role(ids[i], gid));
        if (reply(sock, line) < 0) return -1;
    }
    return reply(sock, "END\n");
}
