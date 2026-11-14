
#include "api.h"
#include <assert.h>
#include <stdio.h>

int main()
{
    assert(group_create(1, "teamA") == 1);
    assert(group_create(1, "teamA") == SS_ERR_EXISTS);
    assert(group_create(0, "teamB") == SS_ERR_INVALID);
    assert(group_create(1, "a b") == SS_ERR_INVALID);
    assert(group_create(2, "teamA") == 2);

    printf("GROUP CREATE TEST PASS\n");
    return 0;
}
