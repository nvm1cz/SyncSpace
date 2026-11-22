
#include "api.h"
#include "groups_ext.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    // User 1 tạo nhóm
    assert(group_create(1, "TeamA") == 1);

    // User 2 chưa thuộc nhóm
    assert(group_role(2, 1) == 0);

    // User 2 xin tham gia
    assert(group_request_join(2, 1) == 0);

    // Không được gửi yêu cầu trùng
    assert(group_request_join(2, 1) == SS_ERR_EXISTS);

    // User 3 không phải trưởng nhóm nên không được duyệt
    assert(group_approve_join(3, 1, 2) == SS_ERR_INVALID);

    // User 1 duyệt yêu cầu
    assert(group_approve_join(1, 1, 2) == 0);

    // User 2 trở thành thành viên
    assert(group_role(2, 1) == 1);

    // Trưởng nhóm vẫn có quyền cao nhất
    assert(group_role(1, 1) == 2);

    // Thành viên không thể xin tham gia lại
    assert(group_request_join(2, 1) == SS_ERR_EXISTS);

    int member_ids[10];

    int count = group_list_members(1, member_ids, 10);

    assert(count == 2);
    assert(member_ids[0] == 1);
    assert(member_ids[1] == 2);

    assert(group_list_members(1, member_ids, 1) == SS_ERR_FULL);
    assert(group_list_members(999, member_ids, 10) == SS_ERR_INVALID);
    
    printf("GROUP MEMBERSHIP TEST PASS\n");
    return 0;
}
