/**
 * @file   svc_mgr_helper.c
 * @brief  服务管理器共享工具函数实现
 *
 * @copyright Copyright (c) 2026
 */

#include "svc_mgr_helper.h"

void svc_mgr_handle_switch_toggle(bool *flag_ptr,
                                   svc_mgr_action_fn enable,
                                   svc_mgr_action_fn disable,
                                   void *ud)
{
    (void)ud;
    if (*flag_ptr) {
        enable();
    } else {
        disable();
    }
}
