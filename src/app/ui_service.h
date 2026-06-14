/**
 * @file   ui_service.h
 * @brief  App 层 UI 公共服务接口
 * @details 为各 user_item App 提供统一的生命周期辅助函数，
 *          减少各 App 中重复的按键重置、标准退出检测等样板代码。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include <stdbool.h>
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief user_item 进入时统一初始化
 * @note  重置按键事件，避免进入前的残留事件被误消费
 */
void ui_service_user_item_init(void);

/**
 * @brief user_item 标准退出检测
 * @param  event_b 按键 B 的当前事件
 * @return true  已触发退出（loop 应直接 return）
 * @return false 未触发退出
 */
bool ui_service_user_item_loop(hal_event_t event_b);

/**
 * @brief user_item 退出时统一清理
 * @note  重置按键事件，避免退出后的残留事件影响菜单导航
 */
void ui_service_user_item_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_H */
