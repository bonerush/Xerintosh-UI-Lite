/**
 * @file   app_menu.c
 * @brief  App 菜单构建实现
 * @details 构建 Xerintosh UI 菜单树：根菜单、设置子菜单、
 *          各 user_item App 入口、波特率子菜单、烧录器引脚子菜单。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_menu.h"

#include <stdint.h>
#include <stdio.h>

#include "app/app_state.h"
#include "app/settings/settings.h"
#include "app/wifi/wifi_manager.h"
#include "app/bluetooth/bt_manager.h"
#include "app/serial_monitor/serial_monitor.h"
#include "app/taskmgr/taskmgr.h"
#include "app/about/about.h"
#include "app/token_usage/token_usage.h"
#include "app/flasher/flasher.h"
#include "app/flasher/flasher_menu.h"
#include "app/oscilloscope/oscilloscope.h"
#include "kernel/kern_init.h"
#include "ui/ui_item.h"

/* 波特率选择回调（前向声明） */
static void on_baud_selected_cb(void *ud);
static bool app_menu_push_checked(xerintosh_list_item_t *parent,
                                  xerintosh_list_item_t *child,
                                  const char *name);

/* ═══ 子菜单构建辅助函数 ═══ */

/**
 * @brief 构建波特率子菜单（6 档 9600-230400）
 * @param parent 父项（通常为"设置"菜单项）
 * @return 波特率子菜单根项，失败返回 NULL
 */
static xerintosh_list_item_t *build_baud_submenu(void)
{
    xerintosh_list_item_t *baud_menu = xerintosh_new_list_item("波特率", list_icon);
    if (baud_menu == NULL) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to create baud menu");
        return NULL;
    }

    const char *baud_labels[] = {"9600", "19200", "38400", "57600", "115200", "230400"};
    int16_t baud_levels[] = {1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 6; i++) {
        xerintosh_list_item_t *btn = xerintosh_new_button_item(
            baud_labels[i], on_baud_selected_cb, default_icon);
        if (btn == NULL) {
            kern_log(KERN_LOG_ERROR, "app_menu: failed to create baud item %s", baud_labels[i]);
            continue;
        }
        btn->user_data = (void *)(intptr_t)baud_levels[i];
        app_menu_push_checked(baud_menu, btn, baud_labels[i]);
    }
    return baud_menu;
}

/**
 * @brief 构建设置子菜单（WiFi/BT 开关、亮度/动画滑块、横屏开关、波特率/烧录器引脚子菜单）
 * @param root 根菜单项
 * @note  仅当 root 非空时执行；失败不影响其他菜单项
 */
static void build_settings_items(xerintosh_list_item_t *root)
{
    if (root == NULL) return;

    xerintosh_list_item_t *sw1 = xerintosh_new_switch_item(
        "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t *sl1 = xerintosh_new_slider_item(
        "亮度", &g_brightness_level, 1, 1, 10,
        NULL, on_brightness_change_cb, default_icon);
    xerintosh_list_item_t *sw_anim = xerintosh_new_switch_item(
        "动画效果", &g_anim_enabled, NULL, on_anim_enabled_change_cb, default_icon);
    xerintosh_list_item_t *sl_anim = xerintosh_new_slider_item(
        "动画速度", &g_anim_speed_level, 1, 1, 10,
        NULL, on_anim_speed_change_cb, default_icon);
    xerintosh_list_item_t *sw_spring = xerintosh_new_switch_item(
        "动画风格", &g_spring_anim_mode, NULL, on_spring_mode_change_cb, default_icon);
    xerintosh_list_item_t *sl_stiff = xerintosh_new_slider_item(
        "弹动硬度", &g_spring_stiffness_level, 1, 1, 10,
        NULL, on_spring_stiffness_change_cb, default_icon);
    xerintosh_list_item_t *sl_damp = xerintosh_new_slider_item(
        "反弹力度", &g_spring_damping_level, 1, 1, 10,
        NULL, on_spring_damping_change_cb, default_icon);
    xerintosh_list_item_t *sw_rot = xerintosh_new_switch_item(
        "横屏/竖屏", &g_is_landscape, NULL, on_screen_rotation_change_cb, default_icon);

    /* 烧录器引脚映射子菜单 */
    flasher_menu_init();
    xerintosh_list_item_t *flasher_pin_menu = flasher_menu_get_root();

    /* 波特率子菜单 */
    xerintosh_list_item_t *baud_menu = build_baud_submenu();

    app_menu_push_checked(root, sw1, "WiFi");
    app_menu_push_checked(root, sl1, "亮度");
    app_menu_push_checked(root, sw_anim, "动画效果");
    app_menu_push_checked(root, sl_anim, "动画速度");
    app_menu_push_checked(root, sw_spring, "动画风格");
    app_menu_push_checked(root, sl_stiff, "弹动硬度");
    app_menu_push_checked(root, sl_damp, "反弹力度");
    app_menu_push_checked(root, sw_rot, "横屏/竖屏");
    app_menu_push_checked(root, flasher_pin_menu, "烧录器引脚");
    app_menu_push_checked(root, baud_menu, "波特率");
}

/**
 * @brief 安全挂载子项；创建失败或挂载失败时打印错误日志
 * @param parent 父项指针
 * @param child  子项指针
 * @param name   子项名称（用于日志）
 * @return true  挂载成功
 * @return false 子项为空或挂载失败
 */
static bool app_menu_push_checked(xerintosh_list_item_t *parent,
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

/**
 * @brief 构建并初始化 Xerintosh UI 菜单树
 * @note  菜单结构：
 *        根菜单
 *        ├── 设置
 *        │   ├── WiFi（开关）
 *        │   ├── 蓝牙（开关）
 *        │   ├── 亮度（滑块 1-10）
 *        │   ├── 动画效果（开关）
 *        │   ├── 动画速度（滑块 1-10）
 *        │   ├── 横屏/竖屏（开关）
 *        │   ├── 烧录器引脚（子菜单）
 *        │   └── 波特率（子菜单）
 *        ├── 任务管理器（user_item）
 *        ├── 串口监视器（user_item）
 *        ├── Token 消耗（user_item）
 *        ├── 烧录器（user_item）
 *        ├── 示波器（user_item）
 *        └── 关于（user_item）
 */
void app_menu_build(void)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root == NULL) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to get root list");
        return;
    }

    /* "设置" 父项（作为 settings 子菜单的容器） */
    xerintosh_list_item_t *item1 = xerintosh_new_list_item("设置", list_icon);

    /* 各 user_item App 入口 */
    xerintosh_list_item_t *items[] = {
        xerintosh_new_user_item("任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon),
        xerintosh_new_user_item("串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit, default_icon),
        xerintosh_new_user_item("Token 消耗", token_usage_init, token_usage_loop, token_usage_exit, default_icon),
        xerintosh_new_user_item("烧录器", flasher_init, flasher_loop, flasher_exit, default_icon),
        xerintosh_new_user_item("示波器", oscilloscope_init, oscilloscope_loop, oscilloscope_exit, default_icon),
        xerintosh_new_user_item("关于", about_init, about_loop, about_exit, user_icon),
    };
    const char *item_names[] = {"任务管理器", "串口监视器", "Token 消耗", "烧录器", "示波器", "关于"};
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++)
        app_menu_push_checked(root, items[i], item_names[i]);

    /* "设置" → 挂载到根菜单后再填充其子项 */
    app_menu_push_checked(root, item1, "设置");
    build_settings_items(item1);
}

/**
 * @brief 波特率子菜单选项确认回调
 * @note  通过 user_data 获取目标波特率等级，设置后触发变更回调并返回父菜单
 */
static void on_baud_selected_cb(void *ud)
{
    int16_t level = (int16_t)(intptr_t)ud;
    g_serial_baud_rate = level;
    on_serial_baud_change_cb(NULL);
    xerintosh_selector_exit_current_item();
}
