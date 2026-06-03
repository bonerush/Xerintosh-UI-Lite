/**
 * @file   app_init.c
 * @brief  App 初始化与输入处理实现
 * @details 构建 Xerintosh UI 菜单树，初始化 WiFi/蓝牙管理器，
 *          并将硬件按键事件映射到 UI 选择器导航。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_init.h"

#include <stddef.h>
#include "settings/settings.h"
#include "storage/storage.h"
#include "wifi/wifi_manager.h"
#include "bluetooth/bt_manager.h"
#include "serial_input/serial_input.h"
#include "serial_monitor/serial_monitor.h"
#include "taskmgr/taskmgr.h"
#include "about/about.h"
#include "shutdown/power_key_popup.h"

#include "ui/ui_item.h"
#include "kernel/kern_task.h"
#include "ui/ui_core.h"
#include "ui/ui_drawer.h"
#include "hal/hal_input.h"
#include "hal/hal_display.h"

/* 由 main.cpp / native_main.cpp 提供的外部变量 */
extern bool g_wifi_on;
extern bool g_bt_on;

/* ─── 设置变更回调（由 main.cpp 提供）─── */
extern void on_brightness_change_cb(void *ud);
extern void on_anim_speed_change_cb(void *ud);
extern void on_anim_enabled_change_cb(void *ud);
extern void on_screen_rotation_change_cb(void *ud);
extern void on_serial_baud_change_cb(void *ud);

/* 波特率选择回调（前向声明）*/
static void on_baud_selected_cb(void *ud);

/* ═══ 菜单构建 ═══ */

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
 *        │   └── 横屏/竖屏（开关）
 *        │   └── 波特率（子菜单）
 *        │       ├── 9600（按钮）
 *        │       ├── 19200（按钮）
 *        │       ├── 38400（按钮）
 *        │       ├── 57600（按钮）
 *        │       ├── 115200（按钮）
 *        │       └── 230400（按钮）
 *        ├── 任务管理器（user_item）
 *        ├── 串口监视器（user_item）
 *        └── 关于（user_item）
 */
void app_init_ui(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    xerintosh_list_item_t* item1 = xerintosh_new_list_item("设置", list_icon);
    xerintosh_list_item_t* item2 = xerintosh_new_user_item(
        "任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon);
    xerintosh_list_item_t* item3 = xerintosh_new_user_item(
        "串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit, default_icon);
    xerintosh_list_item_t* item4 = xerintosh_new_user_item(
        "关于", about_init, about_loop, about_exit, user_icon);

    xerintosh_list_item_t* sw1 = xerintosh_new_switch_item(
        "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t* sw2 = xerintosh_new_switch_item(
        "蓝牙", &g_bt_on, NULL, bt_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t* sl1 = xerintosh_new_slider_item(
        "亮度", &g_brightness_level, 1, 1, 10,
        NULL, on_brightness_change_cb, default_icon);
    xerintosh_list_item_t* sw_anim = xerintosh_new_switch_item(
        "动画效果", &g_anim_enabled, NULL, on_anim_enabled_change_cb, default_icon);
    xerintosh_list_item_t* sl_anim = xerintosh_new_slider_item(
        "动画速度", &g_anim_speed_level, 1, 1, 10,
        NULL, on_anim_speed_change_cb, default_icon);
    xerintosh_list_item_t* sw_rot = xerintosh_new_switch_item(
        "横屏/竖屏", &g_is_landscape, NULL, on_screen_rotation_change_cb, default_icon);

    /* 波特率子菜单 */
    xerintosh_list_item_t* baud_menu = xerintosh_new_list_item("波特率", list_icon);
    const char *baud_labels[] = {"9600", "19200", "38400", "57600", "115200", "230400"};
    int16_t baud_levels[] = {1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 6; i++) {
        xerintosh_list_item_t* btn = xerintosh_new_button_item(
            baud_labels[i], on_baud_selected_cb, default_icon);
        btn->user_data = (void*)(intptr_t)baud_levels[i];
        xerintosh_push_item_to_list(baud_menu, btn);
    }

    xerintosh_push_item_to_list(root, item1);
    xerintosh_push_item_to_list(root, item2);
    xerintosh_push_item_to_list(root, item3);
    xerintosh_push_item_to_list(root, item4);  /* 关于（永远最后） */
    xerintosh_push_item_to_list(item1, sw1);
    xerintosh_push_item_to_list(item1, sw2);
    xerintosh_push_item_to_list(item1, sl1);
    xerintosh_push_item_to_list(item1, sw_anim);
    xerintosh_push_item_to_list(item1, sl_anim);
    xerintosh_push_item_to_list(item1, sw_rot);
    xerintosh_push_item_to_list(item1, baud_menu);
}

/* ═══ 波特率选择回调 ═══ */

/**
 * @brief 波特率子菜单选项确认回调
 * @note  通过 user_data 获取目标波特率等级，设置后触发变更回调并返回父菜单
 */
static void on_baud_selected_cb(void *ud)
{
    (void)ud;
    xerintosh_list_item_t *item = g_xerintosh_selector.selected_item;
    int16_t level = (int16_t)(intptr_t)item->user_data;
    g_serial_baud_rate = level;
    on_serial_baud_change_cb(NULL);
    xerintosh_selector_exit_current_item();
}

/* ═══ 管理器初始化 ═══ */

/**
 * @brief 初始化各管理器并根据存储状态自动启用
 */
void app_init_managers(void)
{
    bt_mgr_init();
    wifi_mgr_init();
    power_key_popup_init();

    /* 先初始化蓝牙（Classic BT SPP），再给 WiFi 初始化，
       避免 WiFi 占用大量 RAM 后 Bluedroid BLE 初始化分配失败
       触发 ESP-IDF v4.4 的 vQueueDelete(NULL) bug */
    /* BT 初始化已移至 deferred_kernel_init()（内核任务 spawn 之后），
       避免在 setup() 中过早消耗内存导致 FreeRTOS 任务创建失败。 */
    if (g_wifi_on) wifi_mgr_enable();

    /* WiFi/BT 内核任务由 setup() 在 xerintosh_init_core() 之后启动，
       确保 g_xerintosh_selector 已初始化，避免 LoadStoreError */
}

/* ═══ 输入处理 ═══ */

/**
 * @brief 处理按键输入事件，映射到 UI 选择器操作
 * @note  按键映射：
 *        - Btn B 短按：选择器上移（上一项）
 *        - Btn B 长按：退出当前项 / 取消输入
 *        - Btn A 短按：选择器下移（下一项）
 *        - Btn A 长按：确认/进入当前项
 */
void app_input_process(void)
{
    /* 无论处于何种模式，每帧都必须调用 hal_input_update()，
       确保 M5.update() 刷新按键边沿标志。
       user_item 内部虽然不处理框架导航，但 user_item 自身
       的 loop 会调用 hal_input_get_event() 读取按键。 */
    hal_input_update();

    /* 更新电源键弹窗（检测 A+B 双键关机事件） */
    power_key_popup_update();

    /* 双键按住模式下，隔离所有正常按钮事件，防止 UI 抖动 */
    if (power_key_popup_is_dual_active()) {
        return;
    }

    /* 若处于 user_item 内部，框架输入由 App 自身接管 */
    if (xerintosh_is_in_user_item()) {
        return;
    }

    /* 进/退场动画期间禁止框架输入，避免误触发 */
    if (!g_xerintosh_exit_animation_finished) {
        return;
    }

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_prev_item();
    }
    else if (event_b == HAL_EVENT_LONG_PRESS)
    {
        /* 若 WiFi 或蓝牙正在等待串口输入，先取消输入 */
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
