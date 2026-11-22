
#include "api.h"
#include <assert.h>
#include <stdio.h>

extern int mock_storage_fail;
extern int mock_storage_calls;

int main(void)
{
    GroupInfo list[10];

    mock_storage_fail = 1;

    assert(group_create(1, "TeamA") == SS_ERR_IO);
    assert(group_list(1, list, 10) == 0);
    assert(group_role(1, 1) == -1);

    mock_storage_fail = 0;

    assert(group_create(1, "TeamA") == 1);
    assert(group_role(1, 1) == 2);
    assert(group_create(1, "TeamA") == SS_ERR_EXISTS);
    assert(group_create(2, "TeamB") == 2);

    assert(mock_storage_calls == 3);

    printf("GROUP TRANSACTION TEST PASS\n");
    return 0;
}
