/**
 * @file   svc_mgr_helper.h
 * @brief  服务管理器共享工具函数
 * @details 提供 WiFi/BT 等服务管理器的通用操作。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SVC_MGR_HELPER_H
#define SVC_MGR_HELPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  通用开关切换处理
 * @param  flag_ptr  开关状态指针（如 g_wifi_on）
 * @param  enable    启用函数
 * @param  disable   禁用函数
 * @param  ud        用户数据（来自回调）
 */
typedef void (*svc_mgr_action_fn)(void);
extern void svc_mgr_handle_switch_toggle(bool *flag_ptr,
                                          svc_mgr_action_fn enable,
                                          svc_mgr_action_fn disable,
                                          void *ud);

#ifdef __cplusplus
}
#endif

#endif /* SVC_MGR_HELPER_H */
