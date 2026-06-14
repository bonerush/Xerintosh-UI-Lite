/**
 * @file   svc_mgr_helper.c
 * @brief  App 上层统一服务助手实现
 * @details 实现 BT 等系统服务的懒加载生命周期封装。
 *
 * @copyright Copyright (c) 2026
 */

#include <stddef.h>

#include "app/svc_mgr_helper.h"
#include "app/bluetooth/bt_manager.h"

void svc_mgr_bt_request_enable(bool *lazy_inited)
{
    if (lazy_inited == NULL) {
        return;
    }

    if (!bt_mgr_is_enabled()) {
        bt_mgr_request_enable();
        *lazy_inited = true;
    }
}

void svc_mgr_bt_request_disable(bool *lazy_inited)
{
    if (lazy_inited == NULL) {
        return;
    }

    if (*lazy_inited && bt_mgr_is_enabled()) {
        bt_mgr_request_disable();
        *lazy_inited = false;
    }
}
