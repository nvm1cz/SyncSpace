#ifndef SYNCSPACE_COMMANDS_STORAGE_H
#define SYNCSPACE_COMMANDS_STORAGE_H

int cmd_mkdir(int sock, int uid, int gid, const char *name);
int cmd_list_files(int sock, int uid, int gid, const char *rel);
int cmd_upload(int sock, int uid, int gid, const char *name,
               unsigned long long size);
int cmd_download(int sock, int uid, int gid, const char *name);

#endif
