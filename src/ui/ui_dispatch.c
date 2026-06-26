/**
 * @file   ui_dispatch.c
 * @brief  Xerintosh UI 类型派发表
 * @details 使用函数指针数组（vtable）替代所有内联 switch(type) 分支，
 *          集中管理 enter / input_next / input_prev / input_exit / measure /
 *          draw / draw_overlay / destroy 八种生命周期行为。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"
#include "ui_core.h"
#include "ui_dirty.h"
#include "ui_drawer.h"
#include "kernel/kern_task.h"

#include <stdio.h>

/* ═══ 工具函数 ═══ */

/**
 * @brief 轻量级 int16_t → 字符串转换，替代 sprintf 减少代码体积
 * @param val  待转换的值
 * @param buf  输出缓冲区（至少 7 字节："-32768\0"）
 * @return     输出缓冲区指针
 * @note   热路径（每帧渲染滑块时调用），避免 snprintf 的格式解析开销
 */
static char *dispatch_itoa(int16_t val, char *buf)
{
    char *p = buf;
    int32_t v = val;  /* 使用 32 位中转，避免 INT16_MIN 取反溢出 */
    if (v < 0) { *p++ = '-'; v = -v; }
    if (v >= 10000) { *p++ = (char)('0' + v / 10000); v %= 10000; }
    if (v >= 1000)  { *p++ = (char)('0' + v / 1000);  v %= 1000;  }
    if (v >= 100)   { *p++ = (char)('0' + v / 100);   v %= 100;   }
    if (v >= 10)    { *p++ = (char)('0' + v / 10);    v %= 10;    }
    *p++ = (char)('0' + v);
    *p = '\0';
    return buf;
}

/* ═══ enter ═══ */

static void dispatch_enter_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
    if (user == NULL) return;
    g_xerintosh_exit_animation_finished = false;
    g_xerintosh_exit_animation_status = 0;
    user->entering_user_item = true;
    user->exiting_user_item = false;
    if (user->kernel_pid == KERN_PID_INVALID)
        user->kernel_pid = kern_task_register_virtual(user->base_item.content);
}

static void dispatch_enter_switch(xerintosh_list_item_t *item)
{
    xerintosh_switch_item_t *sw = xerintosh_to_switch_item(item);
    if (sw == NULL) return;
    *sw->value = !*sw->value;
    if (sw->exit_function) sw->exit_function(item->user_data);
}

static void dispatch_enter_button(xerintosh_list_item_t *item)
{
    xerintosh_button_item_t *btn = xerintosh_to_button_item(item);
    if (btn == NULL) return;
    if (btn->exit_function) btn->exit_function(item->user_data);
}

static void dispatch_enter_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (sl == NULL) return;
    if (!sl->is_confirmed) {
        sl->is_confirmed = true;
        sl->value_backup = *sl->value;
        return;
    }
    if (sl->exit_function) sl->exit_function(item->user_data);
    sl->is_confirmed = false;
}

static void dispatch_enter_list(xerintosh_list_item_t *item)
{
    if (item->child_num == 0) return;
    g_xerintosh_refresh_list_value = true;
    for (uint8_t i = 0; i < item->child_num; i++)
        item->child_list_item[i]->y_list_item = 0;
    g_xerintosh_selector.selected_index = 0;
    g_xerintosh_selector.selected_item = item->child_list_item[0];

    /* 弹簧动画：进入子菜单时清零速度，保证入场和回退动画从零弹起 */
    g_xerintosh_selector.v_y_selector = 0.0f;
    g_xerintosh_selector.v_w_selector = 0.0f;
    g_xerintosh_selector.v_h_selector = 0.0f;

    if (item->init_function) {
        item->init_function(item->user_data);
    }
}

/* ═══ input helpers ═══ */

static bool dispatch_input_next_default(xerintosh_list_item_t *item)
{
    (void)item;
    return false;
}

static bool dispatch_input_next_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (sl == NULL || !sl->is_confirmed) return false;
    *sl->value += sl->value_step;
    if (*sl->value >= sl->value_max)
        *sl->value = sl->value_max;
    return true;
}

static bool dispatch_input_next_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
    return (user != NULL) && user->in_user_item;
}

static bool dispatch_input_prev_default(xerintosh_list_item_t *item)
{
    (void)item;
    return false;
}

static bool dispatch_input_prev_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (sl == NULL || !sl->is_confirmed) return false;
    *sl->value -= sl->value_step;
    if (*sl->value <= sl->value_min)
        *sl->value = sl->value_min;
    return true;
}

static bool dispatch_input_prev_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
    return (user != NULL) && user->in_user_item;
}

static bool dispatch_input_exit_default(xerintosh_list_item_t *item)
{
    (void)item;
    return false;
}

/**
 * @brief 处理 user_item 退出状态重置
 * @param user_item 目标 user_item
 */
static void handle_user_item_exit(xerintosh_user_item_t *user_item)
{
    g_xerintosh_exit_animation_finished = false;
    user_item->entering_user_item = false;
    user_item->exiting_user_item = true;

    /* 注销虚任务，从内核任务链表移除 */
    if (user_item->kernel_pid != KERN_PID_INVALID) {
        kern_task_unregister_virtual(user_item->kernel_pid);
        user_item->kernel_pid = KERN_PID_INVALID;
    }
}

static bool dispatch_input_exit_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (sl == NULL || !sl->is_confirmed) return false;
    sl->is_confirmed = false;
    *sl->value = sl->value_backup;
    return true;
}

static bool dispatch_input_exit_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
    if (user == NULL || !user->in_user_item) return false;
    handle_user_item_exit(user);
    return true;
}

/* ═══ measure ═══ */

static int16_t dispatch_measure_text(xerintosh_list_item_t *item)
{
    /* 仅当选中项内容指针变化时才重新测量字符串宽度 */
    if (g_xerintosh_cached_selector_content != item->content) {
        g_xerintosh_cached_selector_content = item->content;
        g_xerintosh_cached_selector_width = hal_get_string_width(item->content);
    }
    return g_xerintosh_cached_selector_width + 12;
}

static int16_t dispatch_measure_full_width(xerintosh_list_item_t *item)
{
    (void)item;
    return HAL_SCREEN_WIDTH - 18;
}

/* ═══ draw ═══ */

static void dispatch_draw_icon_only(xerintosh_list_item_t *item, int16_t x, int16_t y)
{
    if (xerintosh_is_item_visible(y))
        xerintosh_draw_list_icon(item->icon, x, y);
}

static void dispatch_draw_switch(xerintosh_list_item_t *item, int16_t x, int16_t y)
{
    xerintosh_switch_item_t *sw = xerintosh_to_switch_item(item);
    if (sw == NULL) return;

    if (sw->init_function && g_xerintosh_refresh_list_value)
        sw->init_function(sw->base_item.user_data);
    if (!xerintosh_is_item_visible(y)) return;

    xerintosh_draw_list_icon(sw->base_item.icon, x, y);

    /* 绘制开关外框 */
    g_xerintosh_draw_color = COLOR_FG;
    hal_draw_rect(HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 7, y - 2, 11, 7, g_xerintosh_draw_color);
    if (*sw->value)
    {
        /* 开启态：方块靠右 */
        hal_draw_fill_rect(HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 1, y, 3, 3, g_xerintosh_draw_color);
        hal_draw_pixel(HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 4, y + 1, g_xerintosh_draw_color);
    }
    else
    {
        /* 关闭态：方块靠左 */
        hal_draw_fill_rect(HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 5, y, 3, 3, g_xerintosh_draw_color);
        hal_draw_pixel(HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN, y + 1, g_xerintosh_draw_color);
    }
}

static void dispatch_draw_slider(xerintosh_list_item_t *item, int16_t x, int16_t y)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (sl == NULL) return;

    if (sl->init_function && g_xerintosh_refresh_list_value)
        sl->init_function(sl->base_item.user_data);
    if (!xerintosh_is_item_visible(y)) return;

    xerintosh_draw_list_icon(sl->base_item.icon, x, y);

    /* 已确认的滑块值在选择器 XOR 绘制之后再显示，避免反色伪影 */
    if (!sl->is_confirmed)
    {
        char value_str[10] = {};
        dispatch_itoa(*sl->value, value_str);
        int16_t value_width = hal_get_string_width(value_str);
        int16_t x_value = HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - value_width + 2;
        g_xerintosh_draw_color = COLOR_FG;
        hal_draw_string(x_value + 2, y + hal_get_font_height() / 2, value_str, g_xerintosh_draw_color);
    }
}

static void dispatch_draw_overlay_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (sl == NULL || !sl->is_confirmed) return;

    int16_t y = item->y_list_item + g_xerintosh_camera.y_camera - hal_get_font_height() / 2;
    if (!xerintosh_is_item_visible(y)) return;

    char value_str[10] = {};
    dispatch_itoa(*sl->value, value_str);
    int16_t value_width = hal_get_string_width(value_str);
    int16_t x_value = HAL_SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - value_width + 2;

    /* 反色背景框 */
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_fill_round_rect(x_value, y - 2, value_width + 4, hal_get_font_height() - 2, 1, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_FG;
    hal_draw_string(x_value + 2, y + hal_get_font_height() / 2, value_str, g_xerintosh_draw_color);
}

/* ═══ destroy ═══ */

static void dispatch_destroy_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
    if (user == NULL) return;
    if (user->destroy_callback != NULL && item->user_data != NULL)
        user->destroy_callback(item->user_data);
}

static void dispatch_destroy_default(xerintosh_list_item_t *item)
{
    (void)item;
}

static bool dispatch_right_control_default(xerintosh_list_item_t *item)
{
    (void)item;
    return false;
}

static bool dispatch_right_control_true(xerintosh_list_item_t *item)
{
    (void)item;
    return true;
}

/* ═══ 派发表 ═══ */

typedef struct {
    void (*enter)(xerintosh_list_item_t *);
    bool (*input_next)(xerintosh_list_item_t *);
    bool (*input_prev)(xerintosh_list_item_t *);
    bool (*input_exit)(xerintosh_list_item_t *);
    int16_t (*measure)(xerintosh_list_item_t *);
    void (*draw)(xerintosh_list_item_t *, int16_t, int16_t);
    void (*draw_overlay)(xerintosh_list_item_t *);
    void (*destroy)(xerintosh_list_item_t *);
    bool (*has_right_control)(xerintosh_list_item_t *);
} xerintosh_dispatch_vtable_t;

static const xerintosh_dispatch_vtable_t s_dispatch[] = {
    [list_item]   = {
        .enter             = dispatch_enter_list,
        .input_next        = dispatch_input_next_default,
        .input_prev        = dispatch_input_prev_default,
        .input_exit        = dispatch_input_exit_default,
        .measure           = dispatch_measure_text,
        .draw              = dispatch_draw_icon_only,
        .draw_overlay      = NULL,
        .destroy           = dispatch_destroy_default,
        .has_right_control = dispatch_right_control_default,
    },
    [switch_item] = {
        .enter             = dispatch_enter_switch,
        .input_next        = dispatch_input_next_default,
        .input_prev        = dispatch_input_prev_default,
        .input_exit        = dispatch_input_exit_default,
        .measure           = dispatch_measure_full_width,
        .draw              = dispatch_draw_switch,
        .draw_overlay      = NULL,
        .destroy           = dispatch_destroy_default,
        .has_right_control = dispatch_right_control_true,
    },
    [slider_item] = {
        .enter             = dispatch_enter_slider,
        .input_next        = dispatch_input_next_slider,
        .input_prev        = dispatch_input_prev_slider,
        .input_exit        = dispatch_input_exit_slider,
        .measure           = dispatch_measure_full_width,
        .draw              = dispatch_draw_slider,
        .draw_overlay      = dispatch_draw_overlay_slider,
        .destroy           = dispatch_destroy_default,
        .has_right_control = dispatch_right_control_true,
    },
    [user_item]   = {
        .enter             = dispatch_enter_user,
        .input_next        = dispatch_input_next_user,
        .input_prev        = dispatch_input_prev_user,
        .input_exit        = dispatch_input_exit_user,
        .measure           = dispatch_measure_text,
        .draw              = dispatch_draw_icon_only,
        .draw_overlay      = NULL,
        .destroy           = dispatch_destroy_user,
        .has_right_control = dispatch_right_control_default,
    },
    [button_item] = {
        .enter             = dispatch_enter_button,
        .input_next        = dispatch_input_next_default,
        .input_prev        = dispatch_input_prev_default,
        .input_exit        = dispatch_input_exit_default,
        .measure           = dispatch_measure_text,
        .draw              = dispatch_draw_icon_only,
        .draw_overlay      = NULL,
        .destroy           = dispatch_destroy_default,
        .has_right_control = dispatch_right_control_default,
    },
};

/* ═══ 公开派发函数 ═══ */

static bool type_in_range(xerintosh_list_item_t *item)
{
    return item != NULL && item->type < item_type_count;
}

void xerintosh_dispatch_enter(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return;
    if (s_dispatch[item->type].enter == NULL) return;
    s_dispatch[item->type].enter(item);
    xerintosh_invalidate();  /* 进入新项，UI 状态变化 */
}

bool xerintosh_dispatch_input_next(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return false;
    if (s_dispatch[item->type].input_next == NULL) return false;
    bool consumed = s_dispatch[item->type].input_next(item);
    if (consumed) xerintosh_invalidate();  /* 输入导致 UI 状态变化 */
    return consumed;
}

bool xerintosh_dispatch_input_prev(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return false;
    if (s_dispatch[item->type].input_prev == NULL) return false;
    bool consumed = s_dispatch[item->type].input_prev(item);
    if (consumed) xerintosh_invalidate();
    return consumed;
}

bool xerintosh_dispatch_input_exit(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return false;
    if (s_dispatch[item->type].input_exit == NULL) return false;
    bool consumed = s_dispatch[item->type].input_exit(item);
    if (consumed) xerintosh_invalidate();
    return consumed;
}

int16_t xerintosh_dispatch_measure(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return 0;
    if (s_dispatch[item->type].measure == NULL) return 0;
    return s_dispatch[item->type].measure(item);
}

void xerintosh_dispatch_draw(xerintosh_list_item_t *item, int16_t x, int16_t y)
{
    if (!type_in_range(item)) return;
    if (s_dispatch[item->type].draw == NULL) return;
    s_dispatch[item->type].draw(item, x, y);
}

void xerintosh_dispatch_draw_overlay(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return;
    if (s_dispatch[item->type].draw_overlay == NULL) return;
    s_dispatch[item->type].draw_overlay(item);
}

void xerintosh_dispatch_destroy(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return;
    if (s_dispatch[item->type].destroy == NULL) return;
    s_dispatch[item->type].destroy(item);
}

bool xerintosh_dispatch_has_right_control(xerintosh_list_item_t *item)
{
    if (!type_in_range(item)) return false;
    if (s_dispatch[item->type].has_right_control == NULL) return false;
    return s_dispatch[item->type].has_right_control(item);
}
