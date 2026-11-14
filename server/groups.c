
#include "api.h"
#include <pthread.h>
#include <string.h>

typedef struct {
    int id;
    char name[32];
    int owner_id;
} Group;

static Group groups[100];
static int group_count = 0;
static pthread_mutex_t group_lock = PTHREAD_MUTEX_INITIALIZER;

static int valid_name(const char *name)
{
    if (name == NULL) return 0;

    int len = strlen(name);
    if (len < 1 || len > 31) return 0;

    for (int i = 0; i < len; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

int group_create(int owner_id, const char *name)
{
    if (owner_id <= 0 || !valid_name(name))
        return SS_ERR_INVALID;

    pthread_mutex_lock(&group_lock);

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].owner_id == owner_id &&
            strcmp(groups[i].name, name) == 0)
        {
            pthread_mutex_unlock(&group_lock);
            return SS_ERR_EXISTS;
        }
    }

    if (group_count == 100)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_FULL;
    }

    int id = group_count + 1;

    groups[group_count].id = id;
    groups[group_count].owner_id = owner_id;
    strcpy(groups[group_count].name, name);

    group_count++;

    pthread_mutex_unlock(&group_lock);
    return id;
}
