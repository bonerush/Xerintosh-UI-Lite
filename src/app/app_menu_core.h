/**
 * @file   app_menu_core.h
 * @brief  App 菜单框架内部接口
 * @details 提供菜单树安全挂载辅助函数，供 app_menu_core.c 与
 *          app_menu_entries.c 使用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef APP_MENU_CORE_H
#define APP_MENU_CORE_H

#include <stdbool.h>
#include "ui/ui_item.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 安全挂载子项；失败时打印错误日志
 * @param parent 父项指针
 * @param child  子项指针
 * @param name   子项名称（用于日志）
 * @return true  挂载成功
 * @return false 子项为空或挂载失败
 */
bool app_menu_push_checked(xerintosh_list_item_t *parent,
                           xerintosh_list_item_t *child,
                           const char *name);

#ifdef __cplusplus
}
#endif

#endif /* APP_MENU_CORE_H */
