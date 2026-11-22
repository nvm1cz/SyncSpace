#include "commands_storage.h"
#include "api.h"
#include "net.h"

#include <stdio.h>
#include <string.h>

#define MAX_LIST_ITEMS 256

static int send_line(int sock, const char *line)
{
    return send_all(sock, line, strlen(line));
}

static int check_group_access(int sock, int uid, int gid, int *passed)
{
    *passed = 0;

    if (uid <= 0) {
        return send_line(sock, "ERR|AUTH_REQUIRED\n");
    }
    if (gid <= 0) {
        return send_line(sock, "ERR|BAD_REQUEST\n");
    }

    int role = group_role(uid, gid);
    if (role < 0) {
        return send_line(sock, "ERR|NOT_FOUND\n");
    }
    if (role == 0) {
        return send_line(sock, "ERR|FORBIDDEN\n");
    }

    *passed = 1;
    return 0;
}

int cmd_mkdir(int sock, int uid, int gid, const char *name)
{
    int passed = 0;
    if (check_group_access(sock, uid, gid, &passed) < 0) {
        return -1;
    }
    if (!passed) {
        return 0;
    }

    int rc = storage_mkdir(gid, name);
    if (rc == 0) {
        return send_line(sock, "OK|MKDIR\n");
    }
    if (rc == SS_ERR_EXISTS) {
        return send_line(sock, "ERR|EXISTS\n");
    }
    if (rc == SS_ERR_INVALID) {
        return send_line(sock, "ERR|BAD_REQUEST\n");
    }
    return send_line(sock, "ERR|IO\n");
}

int cmd_list_files(int sock, int uid, int gid, const char *rel)
{
    int passed = 0;
    if (check_group_access(sock, uid, gid, &passed) < 0) {
        return -1;
    }
    if (!passed) {
        return 0;
    }

    if (rel == NULL || strcmp(rel, ".") != 0) {
        return send_line(sock, "ERR|BAD_REQUEST\n");
    }

    FileInfo items[MAX_LIST_ITEMS];
    int n = storage_list(gid, rel, items, MAX_LIST_ITEMS);
    if (n < 0) {
        if (n == SS_ERR_INVALID) {
            return send_line(sock, "ERR|BAD_REQUEST\n");
        }
        return send_line(sock, "ERR|IO\n");
    }

    char header[64];
    int hlen = snprintf(header, sizeof(header), "OK|FILES|%d\n", n);
    if (hlen < 0 || (size_t)hlen >= sizeof(header)) {
        return send_line(sock, "ERR|IO\n");
    }
    if (send_all(sock, header, (size_t)hlen) < 0) {
        return -1;
    }

    for (int i = 0; i < n; i++) {
        char line[256];
        char type = items[i].is_dir ? 'D' : 'F';
        long long size = items[i].is_dir ? 0 : items[i].size;
        int len = snprintf(line, sizeof(line), "ITEM|%c|%s|%lld\n",
                           type, items[i].name, size);
        if (len < 0 || (size_t)len >= sizeof(line)) {
            return -1;
        }
        if (send_all(sock, line, (size_t)len) < 0) {
            return -1;
        }
    }

    return send_line(sock, "END\n");
}

int cmd_upload(int sock, int uid, int gid, const char *name,
               unsigned long long size)
{
    (void)uid;
    (void)gid;
    (void)name;
    (void)size;
    return send_line(sock, "ERR|NOT_IMPLEMENTED\n");
}

int cmd_download(int sock, int uid, int gid, const char *name)
{
    (void)uid;
    (void)gid;
    (void)name;
    return send_line(sock, "ERR|NOT_IMPLEMENTED\n");
}
