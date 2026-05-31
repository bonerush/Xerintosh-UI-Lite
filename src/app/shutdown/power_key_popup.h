/**
 * @file   power_key_popup.h
 * @brief  电源键弹窗模块头文件
 * @details 检测电源键事件，显示按住计时弹窗，长按触发关机。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef POWER_KEY_POPUP_H
#define POWER_KEY_POPUP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化电源键弹窗模块
 */
void power_key_popup_init(void);

/**
 * @brief 更新电源键弹窗状态（每帧调用）
 * @note  检测电源键事件，显示/隐藏弹窗，处理关机逻辑
 */
void power_key_popup_update(void);

/**
 * @brief 查询弹窗是否正在显示
 */
bool power_key_popup_is_visible(void);

/**
 * @brief 查询是否处于双键按住模式（用于隔离正常按钮事件）
 * @return true  双键同时按住中，正常按钮逻辑应被跳过
 * @return false 非双键模式
 */
bool power_key_popup_is_dual_active(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_KEY_POPUP_H */
