/**
 * @file   app_menu_core.c
 * @brief  App 菜单框架实现
 * @details 负责菜单树根节点获取与安全挂载，具体条目注册委托给
 *          app_menu_entries.c。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_menu.h"
#include "app_menu_core.h"
#include "app_menu_entries.h"

#include "kernel/kern_init.h"
#include "ui/ui_item.h"

bool app_menu_push_checked(xerintosh_list_item_t *parent,
                           xerintosh_list_item_t *child,
                           const char *name)
{
    if (child == NULL) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to create item %s", name);
        return false;
    }
    if (!xerintosh_push_item_to_list(parent, child)) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to push item %s", name);
        return false;
    }
    return true;
}

void app_menu_build(void)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root == NULL) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to get root list");
        return;
    }

    app_menu_register_user_item_apps(root);
    app_menu_register_settings_submenu(root);
}
