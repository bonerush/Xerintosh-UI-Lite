/**
 * @file   taskmgr_app.c
 * @brief  任务管理器 App 实现
 * @details 以 user_item 形式运行的任务管理器，显示所有内核任务
 *          （包括虚任务），支持选择并终止非系统关键任务。
 *
 *          保护链：idle / shell / ui / taskmgr / 任务管理器 不可终止。
 *          终止前显示框架 pop_up 确认提示。
 *
 *          架构参考 serial_monitor：init/loop/draw 三层分离。
 *          Phase 1: 集成 ui_anim_row 行列表动画引擎。
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr.h"
#include "taskmgr_ui.h"
#include "kernel/kern_task.h"
#include "kernel/kern_types.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_anim_row.h"

#ifndef NATIVE_TEST
#include "app/wifi/wifi_manager.h"
#include "app/bluetooth/bt_manager.h"
#endif

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ═══ 常量 ═══ */

#define TASKMGR_VISIBLE_MAX  8   /* 最大可见行数 */
#define TASKMGR_CONFIRM_MS 2000  /* 确认超时 */

/* ═══ 全局状态 ═══ */

typedef struct {
    int          selected;       /* 当前选中索引 */
    int          scroll;         /* 列表滚动偏移 */
    int          count;          /* 任务总数（本帧） */
    kern_task_t *tasks[KERN_MAX_TASKS];  /* 本帧任务指针快照 */
    bool         confirming;     /* 是否处于确认态 */
    uint32_t     confirm_tick;   /* 确认开始时间 */
    xerintosh_anim_row_list_t anim_list;  /* 行列表动画上下文 */
    int          prev_selected;  /* 上一帧 selected（用于检测变化） */
    int          prev_scroll;    /* 上一帧 scroll（用于检测变化） */
} taskmgr_state_t;

static taskmgr_state_t g_tm;

/* ═══ 内部函数 ═══ */

/**
 * @brief 刷新任务列表快照
 */
static void taskmgr_refresh_list(void)
{
    g_tm.count = 0;
    kern_task_t *t = kern_task_list_head();
    while (t != NULL && g_tm.count < KERN_MAX_TASKS) {
        if (t->state == KERN_TASK_ZOMBIE && !(t->flags & KERN_TASK_FLAG_VIRTUAL)) {
            t = t->next;
            continue;
        }
        g_tm.tasks[g_tm.count++] = t;
        t = t->next;
    }

    /* 边界保护 */
    if (g_tm.count > 0 && g_tm.selected >= g_tm.count) {
        g_tm.selected = g_tm.count - 1;
    }
    if (g_tm.count == 0) {
        g_tm.selected = 0;
    }
}

/**
 * @brief 滚动条跟踪选中项
 */
static void taskmgr_scroll_to_selected(void)
{
    int visible = taskmgr_visible_lines();
    if (visible > TASKMGR_VISIBLE_MAX) visible = TASKMGR_VISIBLE_MAX;

    if (g_tm.selected >= g_tm.scroll + visible) {
        g_tm.scroll = g_tm.selected - visible + 1;
    }
    if (g_tm.selected < g_tm.scroll) {
        g_tm.scroll = g_tm.selected;
    }
}

/* ═══ 生命周期 ═══ */

void taskmgr_init(void)
{
    g_tm.selected = 0;
    g_tm.scroll   = 0;
    g_tm.confirming = false;
    g_tm.prev_selected = -1;
    g_tm.prev_scroll = -1;

#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif

    taskmgr_refresh_list();

    /* 初始化行列表动画 */
    int visible = taskmgr_visible_lines();
    int16_t list_top = HAL_HEADER_BOTTOM();
    xerintosh_anim_row_list_init(&g_tm.anim_list, visible, TASKMGR_ROW_H, list_top);
}

/* ═══ UI 状态访问器 ═══ */

/* 布局常量（与 taskmgr_ui.c 保持一致，定义在 taskmgr.h） */
/* TASKMGR_HEADER_Y=2, TASKMGR_LEFT_MARGIN=4 */

int taskmgr_visible_lines(void)
{
    int16_t header_h = TASKMGR_HEADER_H;  /* 标题栏高度 */
    int16_t footer_h = TASKMGR_FOOTER_H;  /* 底部信息栏高度 */
    int16_t avail = SCREEN_HEIGHT - header_h - footer_h;
    int16_t row_h = TASKMGR_ROW_H;        /* 行高 */
    int visible = avail / row_h;
    if (visible > TASKMGR_VISIBLE_MAX) visible = TASKMGR_VISIBLE_MAX;
    if (visible < 1) visible = 1;
    return visible;
}

int taskmgr_get_count(void)      { return g_tm.count; }
int taskmgr_get_selected(void)   { return g_tm.selected; }
int taskmgr_get_scroll(void)     { return g_tm.scroll; }

kern_task_t *taskmgr_get_task(int index)
{
    if (index < 0 || index >= g_tm.count) return NULL;
    return g_tm.tasks[index];
}

bool taskmgr_is_confirming(void) { return g_tm.confirming; }

bool taskmgr_is_task_protected(int index)
{
    kern_task_t *t = taskmgr_get_task(index);
    if (t == NULL) return true;
    return kern_task_is_protected(t);
}

const xerintosh_anim_row_list_t *taskmgr_get_anim_list(void)
{
    return &g_tm.anim_list;
}

/* ═══ 生命周期 ═══ */

void taskmgr_loop(void)
{
    /* 第一步：读取按键事件（hal_input_update 由框架 app_input_process 处理） */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* ── 确认态处理 ── */
    if (g_tm.confirming) {
        uint32_t now = hal_get_ticks();
        if (now - g_tm.confirm_tick > TASKMGR_CONFIRM_MS) {
            g_tm.confirming = false;
        } else if (event_a == HAL_EVENT_LONG_PRESS) {
            /* 确认终止 */
            if (g_tm.selected >= 0 && g_tm.selected < g_tm.count) {
                kern_task_t *t = g_tm.tasks[g_tm.selected];
                if (t != NULL && !kern_task_is_protected(t)) {
#ifndef NATIVE_TEST
                    /* 特殊任务：先清理硬件再杀任务 */
                    if (strcmp(t->name, "wifi-mgr") == 0) {
                        wifi_mgr_disable();
                    } else if (strcmp(t->name, "bt-mgr") == 0) {
                        bt_mgr_disable();
                    }
#endif
                    kern_task_kill(t->pid);
                    xerintosh_push_pop_up("Killed", 1000);
                }
            }
            g_tm.confirming = false;
            taskmgr_refresh_list();
        } else if (event_b == HAL_EVENT_LONG_PRESS) {
            g_tm.confirming = false;
        }
        /* 确认态下跳过导航和渲染，由正常 draw 路径处理 */
    }

    /* ── 正常导航态处理 ── */
    if (!g_tm.confirming) {
        if (event_a == HAL_EVENT_SHORT_PRESS && g_tm.count > 0) {
            g_tm.selected = (g_tm.selected + 1) % g_tm.count;
            taskmgr_scroll_to_selected();
        }

        if (event_b == HAL_EVENT_SHORT_PRESS && g_tm.count > 0) {
            g_tm.selected = (g_tm.selected - 1 + g_tm.count) % g_tm.count;
            taskmgr_scroll_to_selected();
        }

        if (event_a == HAL_EVENT_LONG_PRESS) {
            if (g_tm.selected >= 0 && g_tm.selected < g_tm.count) {
                kern_task_t *t = g_tm.tasks[g_tm.selected];
                if (t != NULL && !kern_task_is_protected(t)) {
                    g_tm.confirming = true;
                    g_tm.confirm_tick = hal_get_ticks();
                } else {
                    xerintosh_push_pop_up("Protected", 1000);
                }
            }
        }

        if (event_b == HAL_EVENT_LONG_PRESS) {
            xerintosh_user_item_t* current =
                xerintosh_to_user_item(g_xerintosh_selector.selected_item);
            if (current != NULL && !current->exiting_user_item) {
                xerintosh_selector_exit_current_item();
            }
            return;
        }
    }

    /* ── 动画更新 ── */

    /* 当 selected/scroll 变化时刷新目标位置 */
    if (g_tm.prev_selected != g_tm.selected || g_tm.prev_scroll != g_tm.scroll) {
        /* scroll 变化时注入滚动偏移动画：文字整体滑入/滑出，选择框保持不动 */
        if (g_tm.prev_scroll != g_tm.scroll && g_tm.prev_scroll >= 0) {
            int delta = g_tm.scroll - g_tm.prev_scroll;
            if (delta > 1 || delta < -1) {
                /* wrap-around（列表首尾回绕）：从屏幕底部重新入场，避免溢出列表区域 */
                for (int i = 0; i < g_tm.anim_list.visible_count && i < ANIM_ROW_MAX; i++) {
                    g_tm.anim_list.rows[i].y = (float)SCREEN_HEIGHT;
                }
            } else {
                for (int i = 0; i < g_tm.anim_list.visible_count && i < ANIM_ROW_MAX; i++) {
                    g_tm.anim_list.rows[i].y += (float)(delta * g_tm.anim_list.row_height);
                }
            }
        }

        xerintosh_anim_row_list_refresh(&g_tm.anim_list,
            g_tm.selected, g_tm.scroll, SCREEN_WIDTH, g_tm.count);
        g_tm.prev_selected = g_tm.selected;
        g_tm.prev_scroll = g_tm.scroll;
    }

    /* 每帧更新动画 */
    xerintosh_anim_row_list_update(&g_tm.anim_list, (float)ANIM_SPEED_SELECTOR);

    /* 第二步：绘制界面（clear/flush 由框架 ui_task 处理） */
    taskmgr_draw();
}

void taskmgr_exit(void)
{
    g_tm.confirming = false;
    g_tm.prev_selected = -1;
    g_tm.prev_scroll = -1;
#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif
}
