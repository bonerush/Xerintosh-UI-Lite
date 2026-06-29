/**
 * @file   app_menu_entries.c
 * @brief  App 菜单条目注册实现
 * @details 集中管理设置子菜单、波特率子菜单与各 user_item App 的注册。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_menu_entries.h"
#include "app_menu_core.h"
#include "user_item_contract.h"

#include <stdint.h>
#include <stdio.h>

#include "app/app_state.h"
#include "app/settings/settings.h"
#include "app/wifi/wifi_manager.h"
#include "app/serial_monitor/serial_monitor.h"
#include "app/bluetooth/bt_uart_service.h"
#include "app/taskmgr/taskmgr.h"
#include "app/about/about.h"
#include "app/token_usage/token_usage.h"
#include "app/flasher/flasher.h"
#include "app/flasher/flasher_menu.h"
#include "app/oscilloscope/oscilloscope.h"
#include "kernel/kern_init.h"
#include "ui/ui_item.h"
#include "ui/theme_icon.h"

/* 波特率选择回调 */
static void on_baud_selected_cb(void *ud);

static xerintosh_list_item_t *build_baud_submenu(void)
{
    xerintosh_list_item_t *baud_menu = xerintosh_new_list_item("波特率", list_icon);
    if (baud_menu == NULL) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to create baud menu");
        return NULL;
    }

    const int32_t *baud_table = settings_serial_baud_table();
    int baud_count = settings_serial_baud_count();
    for (int i = 0; i < baud_count; i++) {
        char label[16];
        snprintf(label, sizeof(label), "%ld", (long)baud_table[i]);
        xerintosh_list_item_t *btn = xerintosh_new_button_item(
            label, on_baud_selected_cb, default_icon);
        if (btn == NULL) {
            kern_log(KERN_LOG_ERROR, "app_menu: failed to create baud item %s", label);
            continue;
        }
        btn->user_data = (void *)(intptr_t)(i + 1);
        app_menu_push_checked(baud_menu, btn, label);
    }
    return baud_menu;
}

/* BT/WiFi 互斥：记录启用 BT 前 WiFi 的状态，关闭 BT 后恢复 */
static bool s_wifi_was_on_before_bt = false;

static void bt_mgr_enable(void)
{
    /* BT/WiFi 互斥：启用 BT 前关闭 WiFi 释放内存 */
    s_wifi_was_on_before_bt = wifi_mgr_is_enabled();
    if (s_wifi_was_on_before_bt) {
        wifi_mgr_disable();
    }

    if (!bt_uart_service_init()) {
        /* 初始化失败，恢复 WiFi */
        if (s_wifi_was_on_before_bt) {
            wifi_mgr_enable();
        }
    }
}

static void bt_mgr_disable(void)
{
    bt_uart_service_deinit();

    /* BT/WiFi 互斥：BT 关闭后恢复 WiFi */
    if (s_wifi_was_on_before_bt) {
        wifi_mgr_enable();
        s_wifi_was_on_before_bt = false;
    }
}

static void bt_mgr_on_switch_toggle(void *ud)
{
    (void)ud;
    if (g_bt_on) {
        bt_mgr_enable();
    } else {
        bt_mgr_disable();
    }
}

static void build_settings_items(xerintosh_list_item_t *root)
{
    if (root == NULL) return;

    xerintosh_list_item_t *sw1 = xerintosh_new_switch_item(
        "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t *sw_bt = xerintosh_new_switch_item(
        "蓝牙", &g_bt_on, NULL, bt_mgr_on_switch_toggle, default_icon);
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

    xerintosh_list_item_t *sw_theme = xerintosh_new_switch_item(
        "黑夜/白天", &g_theme_dark, NULL, on_theme_change_cb, custom_icon);
    if (sw_theme) {
        sw_theme->bitmap_data = icon_theme_bitmap;
        sw_theme->bitmap_w = ICON_THEME_WIDTH;
        sw_theme->bitmap_h = ICON_THEME_HEIGHT;
    }

    /* 烧录器引脚映射子菜单 */
    flasher_menu_init();
    xerintosh_list_item_t *flasher_pin_menu = flasher_menu_get_root();

    /* 波特率子菜单 */
    xerintosh_list_item_t *baud_menu = build_baud_submenu();

    app_menu_push_checked(root, sw1, "WiFi");
    app_menu_push_checked(root, sw_bt, "蓝牙");
    app_menu_push_checked(root, sl1, "亮度");
    app_menu_push_checked(root, sw_anim, "动画效果");
    app_menu_push_checked(root, sl_anim, "动画速度");
    app_menu_push_checked(root, sw_spring, "动画风格");
    app_menu_push_checked(root, sl_stiff, "弹动硬度");
    app_menu_push_checked(root, sl_damp, "反弹力度");
    app_menu_push_checked(root, sw_rot, "横屏/竖屏");
    app_menu_push_checked(root, sw_theme, "黑夜/白天");
    app_menu_push_checked(root, flasher_pin_menu, "烧录器引脚");
    app_menu_push_checked(root, baud_menu, "波特率");
}

void app_menu_register_settings_submenu(xerintosh_list_item_t *root)
{
    if (root == NULL) return;
    xerintosh_list_item_t *settings = xerintosh_new_list_item("设置", list_icon);
    app_menu_push_checked(root, settings, "设置");
    build_settings_items(settings);
}

static const user_item_contract_t s_user_item_apps[] = {
    {"任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit},
    {"串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit},
    {"Token 消耗", token_usage_init, token_usage_loop, token_usage_exit},
    {"烧录器", flasher_init, flasher_loop, flasher_exit},
    {"示波器", oscilloscope_init, oscilloscope_loop, oscilloscope_exit},
    {"关于", about_init, about_loop, about_exit},
};

void app_menu_register_user_item_apps(xerintosh_list_item_t *root)
{
    if (root == NULL) return;

    /* 与拆分前保持一致的图标选择：任务管理器/关于使用 user_icon，其余使用 default_icon */
    static const xerintosh_list_item_icon_t s_user_item_icons[] = {
        user_icon,
        default_icon,
        default_icon,
        default_icon,
        default_icon,
        user_icon,
    };

    for (size_t i = 0; i < sizeof(s_user_item_apps) / sizeof(s_user_item_apps[0]); i++) {
        const user_item_contract_t *c = &s_user_item_apps[i];
        xerintosh_list_item_t *item = xerintosh_new_user_item(
            c->name, c->init, c->loop, c->exit, s_user_item_icons[i]);
        app_menu_push_checked(root, item, c->name);
    }
}

static void on_baud_selected_cb(void *ud)
{
    int16_t level = (int16_t)(intptr_t)ud;
    g_serial_baud_rate = level;
    on_serial_baud_change_cb(NULL);
    xerintosh_selector_exit_current_item();
}
