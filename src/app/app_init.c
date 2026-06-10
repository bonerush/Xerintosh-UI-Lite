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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "settings/settings.h"
#include "storage/storage.h"
#include "wifi/wifi_manager.h"
#include "bluetooth/bt_manager.h"
#include "serial_input/serial_input.h"
#include "serial_monitor/serial_monitor.h"
#include "taskmgr/taskmgr.h"
#include "about/about.h"
#include "app/token_usage/token_usage.h"
#include "app/shutdown/power_key_popup.h"

#include "app/flasher/flasher.h"
#include "app/flasher/flasher_gpio.h"
#include "ui/ui_item.h"
#include "kernel/kern_task.h"
#include "ui/ui_core.h"
#include "ui/ui_drawer.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "hal/hal_display.h"


/* WiFi 弹窗刷新（定义在 wifi_manager.cpp，UI 任务每帧调用） */
extern void wifi_popup_refresh(void);

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
static void on_flasher_role_selected_cb(void *ud);
static void on_enter_flasher_submenu(void *ud);
static void on_g36_pressed_cb(void *ud);

/* 烧录器引脚标签缓冲区（动态显示当前角色）3 个引脚 */
static char g_pin_label_bufs[3][24];

/* 烧录器角色选项定义 */
typedef struct {
    flasher_signal_t role;
    const char *label;
} flasher_role_option_t;

static const flasher_role_option_t g_role_options[] = {
    {FLASHER_SIG_BOOT, "BOOT/DTR"},
    {FLASHER_SIG_TX,  "TX"},
};
#define FLASHER_ROLE_OPTION_COUNT 2

static char g_sub_label_bufs[3][2][24];

/* 烧录器引脚菜单（用于在回调中更新上级菜单标签） */
static xerintosh_list_item_t *g_flasher_pin_menu = NULL;

/* 烧录器子菜单强制解除状态机 */
typedef enum {
    FLASHER_SUB_IDLE = 0,
    FLASHER_SUB_WAITING_FORCE_RELEASE,
} flasher_sub_state_t;

static flasher_sub_state_t g_flasher_sub_state = FLASHER_SUB_IDLE;
static uint8_t g_flasher_sub_pin = 0;
static flasher_signal_t g_flasher_sub_role = FLASHER_SIG_NONE;
static uint8_t g_flasher_sub_owner_pin = 0;
static uint32_t g_flasher_sub_press_start = 0;

/* 延迟弹窗：按钮回调中不能直接调用 xerintosh_push_pop_up（M5GFX textWidth
   在中断/调度上下文中会触发 FreeRTOS task timeout），改为设置标志位，
   由每帧 app_input_process 统一 push */
static bool g_deferred_popup_pending = false;

/**
 * @brief 更新烧录器引脚按钮标签，显示当前角色
 */
static void safe_set_content(xerintosh_list_item_t *item, const char *text)
{
    if (item == NULL || text == NULL) return;
    size_t len = strlen(text) + 1;
    char *new_content = (char*)malloc(len);
    if (new_content == NULL) return;
    memcpy(new_content, text, len);
    if (item->content != NULL) {
        free((void*)item->content);
    }
    item->content = new_content;
}

static void update_flasher_pin_label(uint8_t pin)
{
    flasher_signal_t role = FLASHER_SIG_NONE;
    for (int i = 0; i < FLASHER_AVAILABLE_PINS; i++) {
        if (g_flasher_pins[i].pin_num == pin) {
            role = g_flasher_pins[i].role;
            break;
        }
    }
    int idx = (pin == 0) ? 0 : (pin == 26) ? 1 : 2;
    const char *pin_name = (pin == 0) ? "G0" : (pin == 26) ? "G26" : "G36";
    snprintf(g_pin_label_bufs[idx], sizeof(g_pin_label_bufs[idx]),
             "%s [%s]", pin_name, settings_flasher_role_label(role));
}

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
 *        │   ├── 横屏/竖屏（开关）
 *        │   ├── 烧录器引脚（子菜单）
 *        │   └── 波特率（子菜单）
 *        ├── 任务管理器（user_item）
 *        ├── 串口监视器（user_item）
 *        ├── Token Usage（user_item）
 *        ├── 烧录器（user_item）
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
    xerintosh_list_item_t* tu_item = xerintosh_new_user_item(
        "Token Usage", token_usage_init, token_usage_loop, token_usage_exit, default_icon);
    xerintosh_list_item_t* flasher_item = xerintosh_new_user_item(
        "烧录器", flasher_init, flasher_loop, flasher_exit, default_icon);
    xerintosh_list_item_t* item4 = xerintosh_new_user_item(
        "关于", about_init, about_loop, about_exit, user_icon);

    xerintosh_list_item_t* sw1 = xerintosh_new_switch_item(
        "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
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

    /* 烧录器引脚映射子菜单 */
    xerintosh_list_item_t* flasher_pin_menu = xerintosh_new_list_item("烧录器引脚", list_icon);
    g_flasher_pin_menu = flasher_pin_menu;
    uint8_t pin_nums[] = {0, 26, 36};
    for (int i = 0; i < 3; i++) {
        update_flasher_pin_label(pin_nums[i]);

        if (i == 2) {
            /* G36: 输入引脚，不可更改，长按直接弹窗提示 */
            xerintosh_list_item_t* pin_item = xerintosh_new_button_item(
                g_pin_label_bufs[i], on_g36_pressed_cb, default_icon);
            pin_item->user_data = (void*)(intptr_t)pin_nums[i];
            xerintosh_push_item_to_list(flasher_pin_menu, pin_item);
        } else {
            /* G0 / G26: 可选 BOOT/DTR 或 TX */
            xerintosh_list_item_t* pin_item = xerintosh_new_list_item(
                g_pin_label_bufs[i], default_icon);
            pin_item->user_data = (void*)(intptr_t)pin_nums[i];
            pin_item->init_function = on_enter_flasher_submenu;

            for (int j = 0; j < FLASHER_ROLE_OPTION_COUNT; j++) {
                snprintf(g_sub_label_bufs[i][j], sizeof(g_sub_label_bufs[i][j]),
                         "%s", g_role_options[j].label);
                xerintosh_list_item_t* role_btn = xerintosh_new_button_item(
                    g_sub_label_bufs[i][j], on_flasher_role_selected_cb, default_icon);
                role_btn->user_data = (void*)(intptr_t)g_role_options[j].role;
                xerintosh_push_item_to_list(pin_item, role_btn);
            }
            xerintosh_push_item_to_list(flasher_pin_menu, pin_item);
        }
    }

    xerintosh_push_item_to_list(root, item1);
    xerintosh_push_item_to_list(root, item2);
    xerintosh_push_item_to_list(root, item3);
    xerintosh_push_item_to_list(root, tu_item);
    xerintosh_push_item_to_list(root, flasher_item);
    xerintosh_push_item_to_list(root, item4);  /* 关于（永远最后） */
    xerintosh_push_item_to_list(item1, sw1);
    xerintosh_push_item_to_list(item1, sl1);
    xerintosh_push_item_to_list(item1, sw_anim);
    xerintosh_push_item_to_list(item1, sl_anim);
    xerintosh_push_item_to_list(item1, sw_rot);
    xerintosh_push_item_to_list(item1, flasher_pin_menu);
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

/* ═══ 烧录器引脚选择回调 ═══ */

/**
 * @brief 进入烧录器端口号子菜单时的初始化回调
 * @note  动态更新子菜单各选项的显示内容，反映当前占用状态
 */
static void on_enter_flasher_submenu(void *ud)
{
    uint8_t pin = (uint8_t)(intptr_t)ud;
    int pin_idx = (pin == 0) ? 0 : (pin == 26) ? 1 : 2;

    if (g_flasher_pin_menu == NULL) return;
    xerintosh_list_item_t *submenu = g_flasher_pin_menu->child_list_item[pin_idx];
    if (submenu == NULL) return;

    for (int j = 0; j < FLASHER_ROLE_OPTION_COUNT; j++) {
        if (j >= submenu->child_num) break;

        flasher_signal_t role = g_role_options[j].role;
        uint8_t owner = flasher_get_pin_for_signal(role);

        char *buf = g_sub_label_bufs[pin_idx][j];
        if (owner == 255) {
            snprintf(buf, sizeof(g_sub_label_bufs[pin_idx][j]), "%s", g_role_options[j].label);
        } else if (owner == pin) {
            snprintf(buf, sizeof(g_sub_label_bufs[pin_idx][j]), "%s [当前]", g_role_options[j].label);
        } else {
            snprintf(buf, sizeof(g_sub_label_bufs[pin_idx][j]), "%s [G%d]", g_role_options[j].label, owner);
        }
        safe_set_content(submenu->child_list_item[j], buf);
    }
}

/**
 * @brief G36 按钮回调：提示用户该引脚不可更改
 * @note  不在回调中直接调用 xerintosh_push_pop_up，而是设置标志位，
 *        由 app_input_process 每帧检查并执行 push。
 */
static void on_g36_pressed_cb(void *ud)
{
    (void)ud;
    g_deferred_popup_pending = true;
}

/**
 * @brief 烧录器角色选项选择回调
 * @note  处理正常分配、当前角色直接返回、以及被占用时的强制解除状态机
 */
static void on_flasher_role_selected_cb(void *ud)
{
    (void)ud;
    xerintosh_list_item_t *item = g_xerintosh_selector.selected_item;
    flasher_signal_t role = (flasher_signal_t)(intptr_t)item->user_data;
    uint8_t pin = (uint8_t)(intptr_t)item->parent->user_data;

    uint8_t owner = flasher_get_pin_for_signal(role);
    if (owner != 255 && owner != pin) {
        /* 被其他端口占用，进入强制解除状态 */
        int pin_idx = (pin == 0) ? 0 : (pin == 26) ? 1 : 2;
        g_flasher_sub_state = FLASHER_SUB_WAITING_FORCE_RELEASE;
        g_flasher_sub_pin = pin;
        g_flasher_sub_role = role;
        g_flasher_sub_press_start = hal_get_ticks();
        safe_set_content(g_flasher_pin_menu->child_list_item[pin_idx], g_pin_label_bufs[pin_idx]);
        g_flasher_sub_owner_pin = owner;
        return;
    }

    /* 当前端口已占用此角色，直接返回上级 */
    if (owner == pin) {
        xerintosh_selector_exit_current_item();
        return;
    }

    /* 正常分配（G36 不支持的角色会返回 false） */
    if (flasher_set_pin_role(pin, role)) {
        flasher_save_pin_config();
        update_flasher_pin_label(pin);
        int pin_idx = (pin == 0) ? 0 : (pin == 26) ? 1 : 2;
        if (g_flasher_pin_menu != NULL && pin_idx < g_flasher_pin_menu->child_num)
            safe_set_content(g_flasher_pin_menu->child_list_item[pin_idx], g_pin_label_bufs[pin_idx]);
        char buf[32];
        snprintf(buf, sizeof(buf), "G%d -> %s", pin, settings_flasher_role_label(role));
        xerintosh_push_pop_up(buf, 800);
    } else {
        xerintosh_push_pop_up("该引脚不支持此功能", 800);
    }
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

    /* 刷新 WiFi 弹窗（跨任务弹窗，每帧 push 以保持显示） */
    wifi_popup_refresh();

    /* 延迟弹窗：按钮回调通过标志位请求 push，在此安全上下文执行 */
    if (g_deferred_popup_pending) {
        g_deferred_popup_pending = false;
        xerintosh_push_pop_up("G36 为输入串口，不可更改", 1500);
    }

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

    /* ── 烧录器子菜单强制解除状态机 ── */
    if (g_flasher_sub_state == FLASHER_SUB_WAITING_FORCE_RELEASE) {
        /* 每帧刷新弹窗（与 power_key_popup 同模式，保持弹窗存活并实时更新倒计时） */
        {
            uint32_t now = hal_get_ticks();
            uint32_t dur = (now >= g_flasher_sub_press_start)
                           ? (now - g_flasher_sub_press_start) : 0;
            uint32_t remaining = (dur < 800) ? (800 - dur) : 0;
            uint32_t sec = remaining / 1000;
            uint32_t dec = (remaining % 1000) / 100;
            char hint[48];
            snprintf(hint, sizeof(hint), "已被 G%d 占用\n长按 %u.%us 解除",
                     g_flasher_sub_owner_pin,
                     (unsigned int)sec, (unsigned int)dec);
            xerintosh_push_pop_up(hint, 300);
        }

        hal_event_t event_b = hal_input_get_event(HAL_BTN_B);
        if (event_b == HAL_EVENT_SHORT_PRESS || event_b == HAL_EVENT_LONG_PRESS) {
            /* 取消强制解除，动画退出弹窗 */
            g_flasher_sub_state = FLASHER_SUB_IDLE;
            xerintosh_dismiss_pop_up();
            return;
        }
        if (hal_input_is_pressed(HAL_BTN_A)) {
            uint32_t dur;
            uint32_t now = hal_get_ticks();
            if (now >= g_flasher_sub_press_start) {
                dur = now - g_flasher_sub_press_start;
            } else {
                dur = 0;
            }
            if (dur >= 800) {
                flasher_set_pin_role(g_flasher_sub_pin, g_flasher_sub_role);
                flasher_save_pin_config();
                update_flasher_pin_label(g_flasher_sub_owner_pin);
                update_flasher_pin_label(g_flasher_sub_pin);
                int owner_idx = (g_flasher_sub_owner_pin == 0) ? 0
                                : (g_flasher_sub_owner_pin == 26) ? 1 : 2;
                int pin_idx = (g_flasher_sub_pin == 0) ? 0
                              : (g_flasher_sub_pin == 26) ? 1 : 2;
                if (g_flasher_pin_menu != NULL) {
                    if (owner_idx < g_flasher_pin_menu->child_num)
                        safe_set_content(g_flasher_pin_menu->child_list_item[owner_idx],
                                         g_pin_label_bufs[owner_idx]);
                    if (pin_idx < g_flasher_pin_menu->child_num)
                        safe_set_content(g_flasher_pin_menu->child_list_item[pin_idx],
                                         g_pin_label_bufs[pin_idx]);
                }
                g_flasher_sub_state = FLASHER_SUB_IDLE;
                xerintosh_dismiss_pop_up();
                xerintosh_selector_exit_current_item();
                xerintosh_push_pop_up("已强制解除并分配", 800);
            }
        } else {
            /* BtnA 已松开，取消强制解除，动画退出弹窗 */
            g_flasher_sub_state = FLASHER_SUB_IDLE;
            xerintosh_dismiss_pop_up();
        }
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
