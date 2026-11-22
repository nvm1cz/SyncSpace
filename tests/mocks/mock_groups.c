#include "api.h"

/*
 * Mock implementation of group_role() for independent Storage/Transfer tests.
 *
 * Conventions (configurable via mock_groups_set_role / mock_groups_reset):
 * - Override table checked first if registered.
 * - Default fallback convention:
 *   - group_id <= 0 or group_id == 999999 -> -1 (group does not exist)
 *   - user_id == 1 -> 2 (group leader)
 *   - user_id == 2 -> 1 (group member)
 *   - any other user_id -> 0 (outsider)
 */

typedef struct {
    int uid;
    int gid;
    int role;
} MockRoleEntry;

static MockRoleEntry overrides[64];
static int override_count = 0;

void mock_groups_reset(void)
{
    override_count = 0;
}

void mock_groups_set_role(int user_id, int group_id, int role)
{
    for (int i = 0; i < override_count; i++) {
        if (overrides[i].uid == user_id && overrides[i].gid == group_id) {
            overrides[i].role = role;
            return;
        }
    }
    if (override_count < 64) {
        overrides[override_count].uid = user_id;
        overrides[override_count].gid = group_id;
        overrides[override_count].role = role;
        override_count++;
    }
}

int group_role(int user_id, int group_id)
{
    for (int i = 0; i < override_count; i++) {
        if (overrides[i].uid == user_id && overrides[i].gid == group_id) {
            return overrides[i].role;
        }
    }

    if (group_id <= 0 || group_id == 999999) {
        return -1;
    }
    if (user_id == 1) {
        return 2;
    }
    if (user_id == 2) {
        return 1;
    }
    return 0;
}
