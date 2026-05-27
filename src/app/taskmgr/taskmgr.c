/**
 * @file   taskmgr.c
 * @brief  任务管理器 App 实现
 * @details 以 user_item 形式运行的任务管理器，显示所有内核任务
 *          （包括虚任务），支持选择并终止非系统关键任务。
 *
 *          保护链：idle / shell / ui / taskmgr 不可终止。
 *          终止前显示确认弹窗，防止误操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr.h"
#include "kernel/kern_task.h"
#include "kernel/kern_types.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include <stdio.h>

/* ═══ 常量 ═══ */

#define TASKMGR_LINE_HEIGHT  14   /* 每行高度（像素） */
#define TASKMGR_HEADER_Y      2   /* 标题栏 Y */
#define TASKMGR_LIST_Y       18   /* 列表起始 Y */
#define TASKMGR_MAX_VISIBLE   8   /* 最大可见行数 */

/* ═══ 内部状态 ═══ */

static int          g_tm_selected = 0;     /* 当前选中索引 */
static int          g_tm_scroll = 0;       /* 列表滚动偏移 */
static int          g_tm_count = 0;        /* 任务总数（本帧） */
static kern_task_t *g_tm_tasks[KERN_MAX_TASKS];  /* 本帧任务指针快照 */
static bool         g_tm_confirming = false;      /* 是否处于确认态 */
static uint32_t     g_tm_confirm_tick = 0;        /* 确认开始时间 */
#define TASKMGR_CONFIRM_TIMEOUT 2000  /* 确认超时（毫秒） */

/* ═══ 前向声明 ═══ */

static void taskmgr_refresh_list(void);

/* ═══ 渲染辅助 ═══ */

/**
 * @brief 任务状态转字符串
 */
static const char *taskmgr_state_str(kern_task_state_t state)
{
    switch (state) {
    case KERN_TASK_READY:    return "READY";
    case KERN_TASK_RUNNING:  return "RUN  ";
    case KERN_TASK_SLEEPING: return "SLEEP";
    case KERN_TASK_BLOCKED:  return "BLOCK";
    case KERN_TASK_ZOMBIE:   return "ZOMBI";
    default:                 return "?????";
    }
}

/* ═══ 渲染 ═══ */

/**
 * @brief 绘制标题栏
 */
static void taskmgr_draw_header(void)
{
    hal_draw_string(2, TASKMGR_HEADER_Y, "Task Manager", COLOR_ACCENT);
    /* 分隔线 */
    hal_draw_line(0, TASKMGR_LIST_Y - 2, SCREEN_WIDTH, TASKMGR_LIST_Y - 2, COLOR_ACCENT);
}

/**
 * @brief 绘制任务列表
 */
static void taskmgr_draw_list(void)
{
    taskmgr_refresh_list();

    int font_h = hal_get_font_height();
    int visible = (SCREEN_HEIGHT - TASKMGR_LIST_Y) / TASKMGR_LINE_HEIGHT;
    if (visible > TASKMGR_MAX_VISIBLE) visible = TASKMGR_MAX_VISIBLE;

    for (int i = 0; i < visible && (i + g_tm_scroll) < g_tm_count; i++) {
        int idx = i + g_tm_scroll;
        kern_task_t *t = g_tm_tasks[idx];
        if (t == NULL) continue;

        int y = TASKMGR_LIST_Y + i * TASKMGR_LINE_HEIGHT;
        bool is_selected = (idx == g_tm_selected);
        bool is_protected = kern_task_is_protected(t);
        uint16_t color = is_protected ? COLOR_ACCENT : COLOR_FG;

        /* 选择器高亮背景 */
        if (is_selected) {
            hal_draw_fill_rect(0, y, SCREEN_WIDTH, (int16_t)TASKMGR_LINE_HEIGHT, COLOR_FG);
            color = COLOR_BG;  /* 选中行反色 */
        }

        /* 格式：PID NAME STATE STACK */
        char line[48];
        if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
            snprintf(line, sizeof(line), "%2d %-12s %s  n/a",
                     t->pid, t->name, taskmgr_state_str(t->state));
        } else {
            snprintf(line, sizeof(line), "%2d %-12s %s  %zu",
                     t->pid, t->name, taskmgr_state_str(t->state),
                     kern_task_stack_usage(t));
        }
        hal_draw_string(2, y, line, color);
    }
}

/**
 * @brief 绘制确认弹窗
 */
static void taskmgr_draw_confirm(void)
{
    if (!g_tm_confirming) return;
    if (g_tm_selected < 0 || g_tm_selected >= g_tm_count) return;

    kern_task_t *t = g_tm_tasks[g_tm_selected];
    if (t == NULL) return;

    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;

    /* 弹窗背景 */
    hal_draw_fill_rect((int16_t)(cx - 60), (int16_t)(cy - 18), 120, 36, COLOR_BG);
    hal_draw_rect((int16_t)(cx - 60), (int16_t)(cy - 18), 120, 36, COLOR_RED);

    char msg[40];
    snprintf(msg, sizeof(msg), "Kill '%s'?", t->name);
    hal_draw_string((int16_t)(cx - 55), (int16_t)(cy - 10), msg, COLOR_RED);

    hal_draw_string((int16_t)(cx - 55), (int16_t)(cy + 4),
                    "Hold A=Yes  B=No", COLOR_FG);
}

/**
 * @brief 绘制底部信息栏
 */
static void taskmgr_draw_footer(void)
{
    int footer_y = SCREEN_HEIGHT - 18;

    if (g_tm_selected >= 0 && g_tm_selected < g_tm_count) {
        kern_task_t *t = g_tm_tasks[g_tm_selected];
        if (t != NULL) {
            char info[64];
            if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
                snprintf(info, sizeof(info), "PID:%d [VTASK] %s  <protected>",
                         t->pid,
                         kern_task_is_protected(t) ? "protected" : "killable");
            } else {
                snprintf(info, sizeof(info), "PID:%d %s  stack:%zu/%zu  %s",
                         t->pid, t->name,
                         kern_task_stack_usage(t), t->stack_size,
                         kern_task_is_protected(t) ? "protected" : "killable");
            }
            hal_draw_string(2, footer_y, info, COLOR_ACCENT);
        }
    }
}

/* ═══ 任务列表刷新 ═══ */

static void taskmgr_refresh_list(void)
{
    g_tm_count = 0;
    kern_task_t *t = kern_task_list_head();
    while (t != NULL && g_tm_count < KERN_MAX_TASKS) {
        /* 跳过 ZOMBIE 任务（虚任务取消注册后已移除） */
        if (t->state == KERN_TASK_ZOMBIE && !(t->flags & KERN_TASK_FLAG_VIRTUAL)) {
            t = t->next;
            continue;
        }
        g_tm_tasks[g_tm_count++] = t;
        t = t->next;
    }

    /* 边界保护 */
    if (g_tm_count > 0 && g_tm_selected >= g_tm_count) {
        g_tm_selected = g_tm_count - 1;
    }
    if (g_tm_count == 0) {
        g_tm_selected = 0;
    }
}

/* ═══ 生命周期 ═══ */

/**
 * @brief 任务管理器初始化
 */
void taskmgr_init(void)
{
    g_tm_selected = 0;
    g_tm_scroll = 0;
    g_tm_confirming = false;

#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif

    taskmgr_refresh_list();
}

/**
 * @brief 任务管理器主循环
 */
void taskmgr_loop(void)
{
    hal_input_update();

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* ── 确认态处理 ── */
    if (g_tm_confirming) {
        uint32_t now = hal_get_ticks();
        if (now - g_tm_confirm_tick > TASKMGR_CONFIRM_TIMEOUT) {
            g_tm_confirming = false;
        } else if (event_a == HAL_EVENT_LONG_PRESS) {
            /* 确认终止 */
            if (g_tm_selected >= 0 && g_tm_selected < g_tm_count) {
                kern_task_t *t = g_tm_tasks[g_tm_selected];
                if (t != NULL && !kern_task_is_protected(t)) {
                    if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
                        kern_task_unregister_virtual(t->pid);
                    } else {
                        t->state = KERN_TASK_ZOMBIE;
                    }
                }
            }
            g_tm_confirming = false;
            taskmgr_refresh_list();
        } else if (event_b == HAL_EVENT_LONG_PRESS) {
            /* 取消 */
            g_tm_confirming = false;
        }
        /* 确认态下不处理导航 */

        /* 渲染 */
        hal_display_clear();
        taskmgr_draw_header();
        taskmgr_draw_list();
        taskmgr_draw_confirm();
        taskmgr_draw_footer();
        hal_display_flush();
        return;
    }

    /* ── 正常导航态处理 ── */

    /* 短按 A：下一项 */
    if (event_a == HAL_EVENT_SHORT_PRESS) {
        if (g_tm_count > 0) {
            g_tm_selected = (g_tm_selected + 1) % g_tm_count;
            /* 滚动条跟随即选 */
            int visible = (SCREEN_HEIGHT - TASKMGR_LIST_Y) / TASKMGR_LINE_HEIGHT;
            if (visible > TASKMGR_MAX_VISIBLE) visible = TASKMGR_MAX_VISIBLE;
            if (g_tm_selected >= g_tm_scroll + visible) {
                g_tm_scroll = g_tm_selected - visible + 1;
            }
            if (g_tm_selected < g_tm_scroll) {
                g_tm_scroll = g_tm_selected;
            }
        }
    }

    /* 短按 B：上一项 */
    if (event_b == HAL_EVENT_SHORT_PRESS) {
        if (g_tm_count > 0) {
            g_tm_selected = (g_tm_selected - 1 + g_tm_count) % g_tm_count;
            if (g_tm_selected < g_tm_scroll) {
                g_tm_scroll = g_tm_selected;
            }
        }
    }

    /* 长按 A：尝试终止选中任务 */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        if (g_tm_selected >= 0 && g_tm_selected < g_tm_count) {
            kern_task_t *t = g_tm_tasks[g_tm_selected];
            if (t != NULL && !kern_task_is_protected(t)) {
                g_tm_confirming = true;
                g_tm_confirm_tick = hal_get_ticks();
            } else {
                /* 受保护任务：显示提示 */
                /* （TODO: 可用 pop_up 显示提示，当前静默忽略） */
            }
        }
    }

    /* 长按 B：退出任务管理器 */
    if (event_b == HAL_EVENT_LONG_PRESS) {
        xerintosh_user_item_t* current =
            xerintosh_to_user_item(g_xerintosh_selector.selected_item);
        if (current != NULL && !current->exiting_user_item) {
            xerintosh_selector_exit_current_item();
        }
        return;
    }

    /* 渲染 */
    hal_display_clear();
    taskmgr_draw_header();
    taskmgr_draw_list();
    taskmgr_draw_footer();
    hal_display_flush();
}

/**
 * @brief 退出任务管理器
 */
void taskmgr_exit(void)
{
    g_tm_confirming = false;
#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif
}
