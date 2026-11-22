#include "api.h"

int mock_storage_fail = 0;
int mock_storage_calls = 0;

int storage_create_group(int group_id)
{
    mock_storage_calls++;

    if (mock_storage_fail)
        return SS_ERR_IO;

    return group_id > 0 ? 0 : SS_ERR_INVALID;
}
