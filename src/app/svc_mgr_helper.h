/**
 * @file   svc_mgr_helper.h
 * @brief  App 上层统一服务助手接口
 * @details 为各 user_item App 提供统一的 BT/WiFi 等服务生命周期管理，
 *          避免各 App 直接调用底层 manager，减少重复代码和生命周期泄漏。
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
 * @brief 请求启用蓝牙并记录懒加载状态
 * @param lazy_inited 指向串口监视器 BT 懒加载标志的指针；
 *                    若当前蓝牙未启用，则调用 bt_mgr_request_enable() 并将
 *                    *lazy_inited 置为 true。
 * @note  lazy_inited 为 NULL 时直接返回，不做任何操作。
 */
void svc_mgr_bt_request_enable(bool *lazy_inited);

/**
 * @brief 请求禁用蓝牙并清除懒加载状态
 * @param lazy_inited 指向串口监视器 BT 懒加载标志的指针；
 *                    若 *lazy_inited 为 true 且蓝牙仍启用，则调用
 *                    bt_mgr_request_disable() 并将 *lazy_inited 置为 false。
 * @note  lazy_inited 为 NULL 时直接返回，不做任何操作。
 */
void svc_mgr_bt_request_disable(bool *lazy_inited);

#ifdef __cplusplus
}
#endif

#endif /* SVC_MGR_HELPER_H */
