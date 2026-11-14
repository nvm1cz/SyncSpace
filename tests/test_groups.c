
#include "api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main()
{
    assert(group_create(1, "teamA") == 1);
    assert(group_create(1, "teamA") == SS_ERR_EXISTS);
    assert(group_create(0, "teamB") == SS_ERR_INVALID);
    assert(group_create(1, "a b") == SS_ERR_INVALID);
    assert(group_create(2, "teamA") == 2);

    assert(group_role(1, 1) == 2);
    assert(group_role(2, 1) == 0);
    assert(group_role(1, 999) == -1);

    GroupInfo list[10];

    int n = group_list(1, list, 10);
    assert(n == 1);
    assert(list[0].id == 1);
    assert(strcmp(list[0].name, "teamA") == 0);
    assert(list[0].role == 2);

    n = group_list(2, list, 10);
    assert(n == 1);
    assert(list[0].id == 2);
    assert(list[0].role == 2);

    assert(group_list(99, list, 10) == 0);
    assert(group_list(1, list, 0) == -1);

    printf("GROUP ROLE & LIST TEST PASS\n");
    return 0;
}
