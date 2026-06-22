/**
 * @file   flasher_menu.h
 * @brief  烧录器引脚配置菜单
 * @details 负责构建"烧录器引脚"子菜单、处理角色选择回调，
 *          以及被占用角色时的"强制解除"长按状态机。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef FLASHER_MENU_H
#define FLASHER_MENU_H

#include <stdbool.h>
#include "ui/ui_item.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化烧录器引脚菜单（构建菜单树）
 * @note  必须在 app_init_ui() 中调用一次
 */
void flasher_menu_init(void);

/**
 * @brief 获取"烧录器引脚"根菜单项
 * @return 已构建的 list_item 指针
 */
xerintosh_list_item_t *flasher_menu_get_root(void);

/**
 * @brief 每帧输入处理（强制解除状态机 + 延迟弹窗）
 * @note  由 app_input_process() 调用
 */
void flasher_menu_process_input(void);

/**
 * @brief 当前是否处于强制解除状态机中
 * @return true  处于强制解除状态，框架导航应被跳过
 * @return false 未处于强制解除状态
 */
bool flasher_menu_is_active(void);

/**
 * @brief 请求显示一次延迟弹窗
 * @note  供 G36 按钮回调调用，避免在中断/调度上下文直接 push 弹窗
 */
void flasher_menu_request_popup(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASHER_MENU_H */
