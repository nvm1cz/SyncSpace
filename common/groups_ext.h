#ifndef SYNCSPACE_GROUPS_EXT_H
#define SYNCSPACE_GROUPS_EXT_H

int group_request_join(int user_id, int group_id);
int group_approve_join(int owner_id, int group_id, int user_id);
int group_list_members(int group_id, int *out, int capacity);
int group_list_requests(int owner_id, int group_id, int *out, int capacity);

#endif