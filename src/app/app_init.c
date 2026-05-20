#include "app_init.h"

#include <stddef.h>
#include "settings.h"
#include "storage.h"
#include "wifi_manager.h"
#include "bt_manager.h"
#include "serial_input.h"

#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "ui/ui_drawer.h"
#include "hal/hal_input.h"
#include "hal/hal_display.h"

/* 由 main.cpp / native_main.cpp 提供的外部变量 */
extern bool wifi_on;
extern bool bt_on;

/* ─── 设置变更回调（由 main.cpp 提供，声明在此供 app_init 内部使用） ─── */
extern void on_brightness_change_cb(void);
extern void on_anim_speed_change_cb(void);
extern void on_anim_enabled_change_cb(void);
extern void on_screen_rotation_change_cb(void);

/* ─── 菜单构建 ─── */

void app_init_ui(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    xerintosh_list_item_t* item1 = xerintosh_new_list_item("设置", list_icon);
    xerintosh_list_item_t* item2 = xerintosh_new_list_item("关于", user_icon);

    xerintosh_list_item_t* sw1 = xerintosh_new_switch_item(
        "WiFi", &wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t* sw2 = xerintosh_new_switch_item(
        "蓝牙", &bt_on, NULL, bt_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t* sl1 = xerintosh_new_slider_item(
        "亮度", &g_brightness_level, 1, 1, 10,
        NULL, on_brightness_change_cb, default_icon);
    xerintosh_list_item_t* sw_anim = xerintosh_new_switch_item(
        "动画效果", &g_anim_enabled, NULL, on_anim_enabled_change_cb, default_icon);
    xerintosh_list_item_t* sl_anim = xerintosh_new_slider_item(
        "动画速度", &g_anim_speed_level, 1, 1, 10,
        NULL, on_anim_speed_change_cb, default_icon);
    xerintosh_list_item_t* sl_rot = xerintosh_new_slider_item(
        "横屏/竖屏", &g_screen_rotation_level, 1, 1, 2,
        on_screen_rotation_change_cb, on_screen_rotation_change_cb, default_icon);

    xerintosh_push_item_to_list(root, item1);
    xerintosh_push_item_to_list(root, item2);
    xerintosh_push_item_to_list(item1, sw1);
    xerintosh_push_item_to_list(item1, sw2);
    xerintosh_push_item_to_list(item1, sl1);
    xerintosh_push_item_to_list(item1, sw_anim);
    xerintosh_push_item_to_list(item1, sl_anim);
    xerintosh_push_item_to_list(item1, sl_rot);
}

/* ─── 管理器初始化 ─── */

void app_init_managers(void)
{
    wifi_mgr_init();
    bt_mgr_init();

    if (wifi_on) wifi_mgr_enable();
    if (bt_on)   bt_mgr_enable();
}

/* ─── 输入处理 ─── */

void app_input_process(void)
{
    hal_input_update();

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_prev_item();
    }
    else if (event_b == HAL_EVENT_LONG_PRESS)
    {
        if (wifi_mgr_is_waiting_input()) {
            serial_cancel();
        } else if (bt_mgr_is_waiting_input()) {
            serial_cancel();
        }
        xerintosh_selector_exit_current_item();
    }

    if (event_a == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_next_item();
    }
    else if (event_a == HAL_EVENT_LONG_PRESS)
    {
        xerintosh_selector_jump_to_selected_item();
    }
}
