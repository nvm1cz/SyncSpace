#include "api.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static void *register_same_user(void *arg)
{
    int *result = arg;
    *result = auth_register("same_user", "password");
    return NULL;
}

int main(void)
{
    assert(auth_register(NULL, "pass") == SS_ERR_INVALID);
    assert(auth_register("", "pass") == SS_ERR_INVALID);
    assert(auth_register("bad|name", "pass") == SS_ERR_INVALID);
    assert(auth_register("bad name", "pass") == SS_ERR_INVALID);
    assert(auth_register("valid", "") == SS_ERR_INVALID);
    assert(auth_register("valid", "bad\rpass") == SS_ERR_INVALID);
    assert(auth_register("valid", "bad\npass") == SS_ERR_INVALID);
    assert(auth_register("abcdefghijklmnopqrstuvwxyz012345", "pass") == SS_ERR_INVALID);
    assert(auth_register("alice", "some password") == 1);
    assert(auth_register("alice", "new password") == SS_ERR_EXISTS);
    assert(auth_login("alice", "some password") == 1);
    assert(auth_login("alice", "new password") < 0);
    assert(auth_login("unknown", "some password") < 0);
    assert(auth_login("alice", NULL) == SS_ERR_INVALID);

    pthread_t first, second;
    int a, b;
    assert(pthread_create(&first, NULL, register_same_user, &a) == 0);
    assert(pthread_create(&second, NULL, register_same_user, &b) == 0);
    assert(pthread_join(first, NULL) == 0);
    assert(pthread_join(second, NULL) == 0);
    assert((a == 2 && b == SS_ERR_EXISTS) || (b == 2 && a == SS_ERR_EXISTS));
    assert(auth_login("same_user", "password") == 2);

    for (int id = 3; id <= 100; id++) {
        char username[32];
        snprintf(username, sizeof(username), "user%d", id);
        assert(auth_register(username, "pass") == id);
    }
    assert(auth_register("overflow", "pass") == SS_ERR_FULL);
    assert(auth_register("alice", "pass") == SS_ERR_EXISTS);
    assert(auth_login("alice", "some password") == 1);

    puts("AUTH TEST PASS");
    return 0;
}
