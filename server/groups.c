
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

int group_role(int user_id, int group_id)
{
    pthread_mutex_lock(&group_lock);

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].id == group_id)
        {
            int role = groups[i].owner_id == user_id ? 2 : 0;
            pthread_mutex_unlock(&group_lock);
            return role;
        }
    }

    pthread_mutex_unlock(&group_lock);
    return -1;
}

int group_list(int user_id, GroupInfo *out, int capacity)
{
    if (user_id <= 0 || capacity < 0 ||
        (capacity > 0 && out == NULL))
        return SS_ERR_INVALID;

    pthread_mutex_lock(&group_lock);

    int count = 0;

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].owner_id != user_id)
            continue;

        if (count >= capacity)
        {
            pthread_mutex_unlock(&group_lock);
            return SS_ERR_INVALID;
        }

        out[count].id = groups[i].id;
        strcpy(out[count].name, groups[i].name);
        out[count].role = 2;
        count++;
    }

    pthread_mutex_unlock(&group_lock);
    return count;
}