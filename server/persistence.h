#ifndef SYNCSPACE_PERSISTENCE_H
#define SYNCSPACE_PERSISTENCE_H

/* Only the real TCP server links the SQLite persistence backend. */
int persistence_init(void);
int persistence_add_account(int id, const char *username, const char *password);
int persistence_add_group(int id, int owner_id, const char *name);
int persistence_group_path_is_unused(int id);
int persistence_add_request(int group_id, int user_id);
int persistence_approve_request(int group_id, int user_id);

/* Called on startup by the backend; never persist while restoring. */
int auth_restore(int id, const char *username, const char *password);
int group_restore(int id, int owner_id, const char *name);
int group_restore_member(int group_id, int user_id);
int group_restore_request(int group_id, int user_id);

#endif
