#ifndef SYNCSPACE_API_H
#define SYNCSPACE_API_H
 
typedef struct {
    int id;
    char name[32];
    int role;
} GroupInfo;
 
typedef struct {
    char name[65];
    int is_dir;
    long long size;
} FileInfo;
 
#define SS_ERR_INVALID -1
#define SS_ERR_EXISTS  -2
#define SS_ERR_FULL    -3
#define SS_ERR_IO      -4
 
int auth_register(const char *username, const char *password);
int auth_login(const char *username, const char *password);
 
int group_create(int owner_id, const char *name);
int group_role(int user_id, int group_id);
int group_list(int user_id, GroupInfo *out, int capacity);
 
int storage_create_group(int group_id);
int storage_list(int group_id, const char *rel,
                 FileInfo *out, int capacity);
int storage_mkdir(int group_id, const char *rel);
 
#endif

