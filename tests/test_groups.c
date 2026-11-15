
#include "api.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define THREADS 4
#define NLOPS 30

typedef struct {
    int thread_no;
    int results[NLOPS];
} TestData;

void *create_groups(void *arg)
{
    TestData *data = arg;

    for (int i = 0; i < NLOPS; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "team%d_%d", data->thread_no, i);
        data->results[i] = group_create(3, name);
    }

    return NULL;
}

int main()
{
    assert(group_create(1, "teamA") == 1);
    assert(group_create(1, "teamA") == SS_ERR_EXISTS);
    assert(group_create(0, "teamB") == SS_ERR_INVALID);
    assert(group_create(1, "") == SS_ERR_INVALID);
    assert(group_create(1, "bad name") == SS_ERR_INVALID);
    assert(group_create(1, "../abc") == SS_ERR_INVALID);
    assert(group_create(1, "a/b") == SS_ERR_INVALID);
    assert(group_create(1, "abcdefghijklmnopqrstuvwxyz123456") == SS_ERR_INVALID);
    assert(group_create(2, "teamA") == 2);

    assert(group_role(1, 1) == 2);
    assert(group_role(2, 1) == 0);
    assert(group_role(1, 999) == -1);

    GroupInfo list[100];
    assert(group_list(1, list, 100) == 1);
    assert(list[0].id == 1);
    assert(strcmp(list[0].name, "teamA") == 0);
    assert(list[0].role == 2);
    assert(group_list(99, list, 100) == 0);

    pthread_t threads[THREADS];
    TestData data[THREADS];

    for (int i = 0; i < THREADS; i++)
    {
        data[i].thread_no = i;
        assert(pthread_create(&threads[i], NULL, create_groups, &data[i]) == 0);
    }

    for (int i = 0; i < THREADS; i++)
        assert(pthread_join(threads[i], NULL) == 0);

    int seen[101] = {0};
    int success = 0, full = 0;

    for (int i = 0; i < THREADS; i++)
    {
        for (int j = 0; j < NLOPS; j++)
        {
            int id = data[i].results[j];

            if (id == SS_ERR_FULL)
            {
                full++;
                continue;
            }

            assert(id >= 3 && id <= 100);
            assert(seen[id] == 0);
            seen[id] = 1;
            assert(group_role(3, id) == 2);
            success++;
        }
    }

    assert(success == 98);
    assert(full == 22);
    assert(group_list(3, list, 100) == 98);
    assert(group_list(3, list, 97) == SS_ERR_INVALID);
    assert(group_create(4, "overflow") == SS_ERR_FULL);

    printf("GROUP STRESS TEST PASS\n");
    return 0;
}
