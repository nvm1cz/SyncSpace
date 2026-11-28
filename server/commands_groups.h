#ifndef COMMANDS_GROUPS_H
#define COMMANDS_GROUPS_H

int cmd_create_group(int sock, int uid, const char *name);
int cmd_list_groups(int sock, int uid);
int cmd_request_join(int sock, int uid, int gid);
int cmd_approve_join(int sock, int uid, int gid, int user_id);
int cmd_list_requests(int sock, int uid, int gid);
int cmd_list_members(int sock, int uid, int gid);

#endif
