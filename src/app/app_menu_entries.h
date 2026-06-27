/**
 * @file   app_menu_entries.h
 * @brief  App 菜单条目注册接口
 * @details 声明设置子菜单与 user_item App 入口的注册函数，由
 *          app_menu_core.c 在构建菜单树时调用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef APP_MENU_ENTRIES_H
#define APP_MENU_ENTRIES_H

#include "ui/ui_item.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 向根菜单注册所有 user_item App 入口
 * @param root 根菜单项指针
 */
void app_menu_register_user_item_apps(xerintosh_list_item_t *root);

/**
 * @brief 向根菜单注册"设置"子菜单及其子项
 * @param root 根菜单项指针
 */
void app_menu_register_settings_submenu(xerintosh_list_item_t *root);

#ifdef __cplusplus
}
#endif

#endif /* APP_MENU_ENTRIES_H */
