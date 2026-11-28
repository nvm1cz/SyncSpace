#include "../common/api.h"
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
typedef struct {
    int user_id;
    int group_id;
} JoinRequest;

static JoinRequest requests[1000];
static int request_count = 0;

typedef struct {
    int user_id;
    int group_id;
} GroupMember;

static GroupMember members[1000];
static int member_count = 0;


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
    if (storage_create_group(id) != 0)
    {
    pthread_mutex_unlock(&group_lock);
    return SS_ERR_IO;
    }

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
            int role = 0;

            if (groups[i].owner_id == user_id)
            {
                role = 2;
            }
            else
            {
                for (int j = 0; j < member_count; j++)
                {
                    if (members[j].group_id == group_id &&
                        members[j].user_id == user_id)
                    {
                        role = 1;
                        break;
                    }
                }
            }

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
        int role = (groups[i].owner_id == user_id) ? 2 : 0;

        if (role == 0)
        {
            for (int j = 0; j < member_count; j++)
            {
                if (members[j].group_id == groups[i].id &&
                    members[j].user_id == user_id)
                {
                    role = 1;
                    break;
                }
            }
        }

        if (role == 0) continue;

        if (count >= capacity)
        {
            pthread_mutex_unlock(&group_lock);
            return SS_ERR_INVALID;
        }

        out[count].id = groups[i].id;
        strcpy(out[count].name, groups[i].name);
        out[count].role = role;
        count++;
    }

    pthread_mutex_unlock(&group_lock);
    return count;
}

int group_request_join(int user_id, int group_id)
{
    if (user_id <= 0 || group_id <= 0)
        return SS_ERR_INVALID;

    pthread_mutex_lock(&group_lock);

    int found = 0;

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].id == group_id)
        {
            found = 1;

            if (groups[i].owner_id == user_id)
            {
                pthread_mutex_unlock(&group_lock);
                return SS_ERR_EXISTS;
            }

            break;
        }
    }

    if (!found)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_INVALID;
    }
    for (int i = 0; i < member_count; i++)
    {
        if (members[i].user_id == user_id &&
            members[i].group_id == group_id)
        {
            pthread_mutex_unlock(&group_lock);
            return SS_ERR_EXISTS;
        }
    }
    
    for (int i = 0; i < request_count; i++)
    {
        if (requests[i].user_id == user_id &&
            requests[i].group_id == group_id)
        {
            pthread_mutex_unlock(&group_lock);
            return SS_ERR_EXISTS;
        }
    }

    if (request_count >= 1000)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_FULL;
    }

    requests[request_count].user_id = user_id;
    requests[request_count].group_id = group_id;
    request_count++;

    pthread_mutex_unlock(&group_lock);
    return 0;
}

int group_approve_join(int owner_id, int group_id, int user_id)
{
    if (owner_id <= 0 || group_id <= 0 || user_id <= 0)
        return SS_ERR_INVALID;

    pthread_mutex_lock(&group_lock);

    int is_owner = 0;

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].id == group_id)
        {
            is_owner = (groups[i].owner_id == owner_id);
            break;
        }
    }

    if (!is_owner)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_INVALID;
    }

    int request_index = -1;

    for (int i = 0; i < request_count; i++)
    {
        if (requests[i].group_id == group_id &&
            requests[i].user_id == user_id)
        {
            request_index = i;
            break;
        }
    }

    if (request_index == -1)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_INVALID;
    }

    if (member_count >= 1000)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_FULL;
    }

    members[member_count].user_id = user_id;
    members[member_count].group_id = group_id;
    member_count++;

    for (int i = request_index; i < request_count - 1; i++)
        requests[i] = requests[i + 1];

    request_count--;

    pthread_mutex_unlock(&group_lock);
    return 0;
}

int group_list_members(int group_id, int *out, int capacity)
{
    if (group_id <= 0 || out == NULL || capacity <= 0)
        return SS_ERR_INVALID;

    pthread_mutex_lock(&group_lock);

    int count = 0;
    int found = 0;

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].id == group_id)
        {
            found = 1;
            out[count++] = groups[i].owner_id;
            break;
        }
    }

    if (!found)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_INVALID;
    }

    for (int i = 0; i < member_count; i++)
    {
        if (members[i].group_id == group_id)
        {
            if (count >= capacity)
            {
                pthread_mutex_unlock(&group_lock);
                return SS_ERR_FULL;
            }

            out[count++] = members[i].user_id;
        }
    }

    pthread_mutex_unlock(&group_lock);
    return count;
}

int group_list_requests(int owner_id, int group_id, int *out, int capacity)
{
    if (owner_id <= 0 || group_id <= 0 ||
        out == NULL || capacity <= 0)
        return SS_ERR_INVALID;

    pthread_mutex_lock(&group_lock);

    int is_owner = 0;

    for (int i = 0; i < group_count; i++)
    {
        if (groups[i].id == group_id &&
            groups[i].owner_id == owner_id)
        {
            is_owner = 1;
            break;
        }
    }

    if (!is_owner)
    {
        pthread_mutex_unlock(&group_lock);
        return SS_ERR_INVALID;
    }

    int count = 0;

    for (int i = 0; i < request_count; i++)
    {
        if (requests[i].group_id == group_id)
        {
            if (count >= capacity)
            {
                pthread_mutex_unlock(&group_lock);
                return SS_ERR_FULL;
            }

            out[count++] = requests[i].user_id;
        }
    }

    pthread_mutex_unlock(&group_lock);
    return count;
}
