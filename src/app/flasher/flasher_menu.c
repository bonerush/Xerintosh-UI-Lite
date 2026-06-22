/**
 * @file   flasher_menu.c
 * @brief  烧录器引脚配置菜单实现
 * @details 构建引脚选择子菜单，处理 BOOT/DTR 与 TX 的角色分配，
 *          以及当被占用时长按 A 强制解除并重新分配的状态机。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher_menu.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flasher_gpio.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

/* ═══ 配置 ═══ */

#define FLASHER_ROLE_OPTION_COUNT 2

typedef struct {
    flasher_signal_t role;
    const char *label;
} flasher_role_option_t;

static const flasher_role_option_t g_role_options[] = {
    {FLASHER_SIG_BOOT, "BOOT/DTR"},
    {FLASHER_SIG_TX,   "TX"},
};

/* ═══ 状态 ═══ */

static xerintosh_list_item_t *s_pin_menu_root = NULL;

/* 3 个引脚（G0/G26/G36）的标签缓冲区 */
static char s_pin_label_bufs[3][24];
/* 子菜单中 BOOT/DTR、TX 两个选项的标签缓冲区 */
static char s_sub_label_bufs[3][2][24];

typedef enum {
    FLASHER_SUB_IDLE = 0,
    FLASHER_SUB_WAITING_FORCE_RELEASE,
} flasher_sub_state_t;

static flasher_sub_state_t s_flasher_sub_state = FLASHER_SUB_IDLE;
static uint8_t s_flasher_sub_pin = 0;
static flasher_signal_t s_flasher_sub_role = FLASHER_SIG_NONE;
static uint8_t s_flasher_sub_owner_pin = 0;
static uint32_t s_flasher_sub_press_start = 0;

/* 延迟弹窗：按钮回调中不能直接调用 xerintosh_push_pop_up（显示层 textWidth
   在中断/调度上下文中会触发 FreeRTOS task timeout），改为设置标志位，
   由每帧 flasher_menu_process_input() 统一 push */
static bool s_deferred_popup_pending = false;

/* ═══ 内部工具 ═══ */

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

static void update_pin_label(uint8_t pin)
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
    snprintf(s_pin_label_bufs[idx], sizeof(s_pin_label_bufs[idx]),
             "%s [%s]", pin_name, flasher_role_label(role));
}

/* ═══ 回调 ═══ */

static void on_g36_pressed_cb(void *ud)
{
    (void)ud;
    s_deferred_popup_pending = true;
}

static void on_enter_flasher_submenu(void *ud)
{
    uint8_t pin = (uint8_t)(intptr_t)ud;
    int pin_idx = (pin == 0) ? 0 : (pin == 26) ? 1 : 2;

    if (s_pin_menu_root == NULL) return;
    xerintosh_list_item_t *submenu = s_pin_menu_root->child_list_item[pin_idx];
    if (submenu == NULL) return;

    for (int j = 0; j < FLASHER_ROLE_OPTION_COUNT; j++) {
        if (j >= submenu->child_num) break;

        flasher_signal_t role = g_role_options[j].role;
        uint8_t owner = flasher_get_pin_for_signal(role);

        char *buf = s_sub_label_bufs[pin_idx][j];
        if (owner == 255) {
            snprintf(buf, sizeof(s_sub_label_bufs[pin_idx][j]), "%s", g_role_options[j].label);
        } else if (owner == pin) {
            snprintf(buf, sizeof(s_sub_label_bufs[pin_idx][j]), "%s [当前]", g_role_options[j].label);
        } else {
            snprintf(buf, sizeof(s_sub_label_bufs[pin_idx][j]), "%s [G%d]", g_role_options[j].label, owner);
        }
        safe_set_content(submenu->child_list_item[j], buf);
    }
}

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
        s_flasher_sub_state = FLASHER_SUB_WAITING_FORCE_RELEASE;
        s_flasher_sub_pin = pin;
        s_flasher_sub_role = role;
        s_flasher_sub_press_start = hal_get_ticks();
        safe_set_content(s_pin_menu_root->child_list_item[pin_idx], s_pin_label_bufs[pin_idx]);
        s_flasher_sub_owner_pin = owner;
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
        update_pin_label(pin);
        int pin_idx = (pin == 0) ? 0 : (pin == 26) ? 1 : 2;
        if (s_pin_menu_root != NULL && pin_idx < s_pin_menu_root->child_num)
            safe_set_content(s_pin_menu_root->child_list_item[pin_idx], s_pin_label_bufs[pin_idx]);
        char buf[32];
        snprintf(buf, sizeof(buf), "G%d -> %s", pin, flasher_role_label(role));
        xerintosh_push_pop_up(buf, 800);
    } else {
        xerintosh_push_pop_up("该引脚不支持此功能", 800);
    }
    xerintosh_selector_exit_current_item();
}

/* ═══ 公共 API ═══ */

void flasher_menu_init(void)
{
    s_pin_menu_root = xerintosh_new_list_item("烧录器引脚", list_icon);

    uint8_t pin_nums[] = {0, 26, 36};
    for (int i = 0; i < 3; i++) {
        update_pin_label(pin_nums[i]);

        if (i == 2) {
            /* G36: 输入引脚，不可更改，长按直接弹窗提示 */
            xerintosh_list_item_t *pin_item = xerintosh_new_button_item(
                s_pin_label_bufs[i], on_g36_pressed_cb, default_icon);
            pin_item->user_data = (void*)(intptr_t)pin_nums[i];
            xerintosh_push_item_to_list(s_pin_menu_root, pin_item);
        } else {
            /* G0 / G26: 可选 BOOT/DTR 或 TX */
            xerintosh_list_item_t *pin_item = xerintosh_new_list_item(
                s_pin_label_bufs[i], default_icon);
            pin_item->user_data = (void*)(intptr_t)pin_nums[i];
            pin_item->init_function = on_enter_flasher_submenu;

            for (int j = 0; j < FLASHER_ROLE_OPTION_COUNT; j++) {
                snprintf(s_sub_label_bufs[i][j], sizeof(s_sub_label_bufs[i][j]),
                         "%s", g_role_options[j].label);
                xerintosh_list_item_t *role_btn = xerintosh_new_button_item(
                    s_sub_label_bufs[i][j], on_flasher_role_selected_cb, default_icon);
                role_btn->user_data = (void*)(intptr_t)g_role_options[j].role;
                xerintosh_push_item_to_list(pin_item, role_btn);
            }
            xerintosh_push_item_to_list(s_pin_menu_root, pin_item);
        }
    }
}

xerintosh_list_item_t *flasher_menu_get_root(void)
{
    return s_pin_menu_root;
}

bool flasher_menu_is_active(void)
{
    return (s_flasher_sub_state == FLASHER_SUB_WAITING_FORCE_RELEASE);
}

void flasher_menu_request_popup(void)
{
    s_deferred_popup_pending = true;
}

void flasher_menu_process_input(void)
{
    /* 延迟弹窗：按钮回调通过标志位请求 push，在此安全上下文执行 */
    if (s_deferred_popup_pending) {
        s_deferred_popup_pending = false;
        xerintosh_push_pop_up("G36 为输入串口，不可更改", 1500);
    }

    if (s_flasher_sub_state != FLASHER_SUB_WAITING_FORCE_RELEASE) return;

    /* 每帧刷新弹窗，保持显示并实时更新倒计时 */
    {
        uint32_t now = hal_get_ticks();
        uint32_t dur = (now >= s_flasher_sub_press_start)
                       ? (now - s_flasher_sub_press_start) : 0;
        uint32_t remaining = (dur < 800) ? (800 - dur) : 0;
        uint32_t sec = remaining / 1000;
        uint32_t dec = (remaining % 1000) / 100;
        char hint[48];
        snprintf(hint, sizeof(hint), "已被 G%d 占用\n长按 %u.%us 解除",
                 s_flasher_sub_owner_pin,
                 (unsigned int)sec, (unsigned int)dec);
        xerintosh_push_pop_up(hint, 300);
    }

    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);
    if (event_b == HAL_EVENT_SHORT_PRESS || event_b == HAL_EVENT_LONG_PRESS) {
        /* 取消强制解除，动画退出弹窗 */
        s_flasher_sub_state = FLASHER_SUB_IDLE;
        xerintosh_dismiss_pop_up();
        return;
    }

    if (hal_input_is_pressed(HAL_BTN_A)) {
        uint32_t dur;
        uint32_t now = hal_get_ticks();
        if (now >= s_flasher_sub_press_start) {
            dur = now - s_flasher_sub_press_start;
        } else {
            dur = 0;
        }
        if (dur >= 800) {
            flasher_set_pin_role(s_flasher_sub_pin, s_flasher_sub_role);
            flasher_save_pin_config();
            update_pin_label(s_flasher_sub_owner_pin);
            update_pin_label(s_flasher_sub_pin);
            int owner_idx = (s_flasher_sub_owner_pin == 0) ? 0
                            : (s_flasher_sub_owner_pin == 26) ? 1 : 2;
            int pin_idx = (s_flasher_sub_pin == 0) ? 0
                          : (s_flasher_sub_pin == 26) ? 1 : 2;
            if (s_pin_menu_root != NULL) {
                if (owner_idx < s_pin_menu_root->child_num)
                    safe_set_content(s_pin_menu_root->child_list_item[owner_idx],
                                     s_pin_label_bufs[owner_idx]);
                if (pin_idx < s_pin_menu_root->child_num)
                    safe_set_content(s_pin_menu_root->child_list_item[pin_idx],
                                     s_pin_label_bufs[pin_idx]);
            }
            s_flasher_sub_state = FLASHER_SUB_IDLE;
            xerintosh_dismiss_pop_up();
            xerintosh_selector_exit_current_item();
            xerintosh_push_pop_up("已强制解除并分配", 800);
        }
    } else {
        /* BtnA 已松开，取消强制解除，动画退出弹窗 */
        s_flasher_sub_state = FLASHER_SUB_IDLE;
        xerintosh_dismiss_pop_up();
    }
}
