/**
 * @file   ui_dispatch.c
 * @brief  Xerintosh UI 类型派发表
 * @details 使用函数指针数组替代内联 switch(type) 分支。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"
#include "ui_core.h"
#include "kernel/kern_task.h"

/* ═══ 派发函数实现 ═══ */

static void dispatch_enter_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
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
    *sw->value = !*sw->value;
    if (sw->exit_function) sw->exit_function(item->user_data);
}

static void dispatch_enter_button(xerintosh_list_item_t *item)
{
    xerintosh_button_item_t *btn = xerintosh_to_button_item(item);
    if (btn->exit_function) btn->exit_function(item->user_data);
}

static void dispatch_enter_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
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
}

/* ═══ 派发表 ═══ */

typedef void (*enter_fn_t)(xerintosh_list_item_t *);

static const enter_fn_t s_enter_dispatch[] = {
    [list_item]   = dispatch_enter_list,
    [switch_item] = dispatch_enter_switch,
    [slider_item] = dispatch_enter_slider,
    [user_item]   = dispatch_enter_user,
    [button_item] = dispatch_enter_button,
};

/* ═══ 公开派发函数 ═══ */

void xerintosh_dispatch_enter(xerintosh_list_item_t *item)
{
    if (item == NULL) return;
    if (item->type > button_item) return;
    s_enter_dispatch[item->type](item);
}
