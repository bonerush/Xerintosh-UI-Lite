/**
 * @file   taskmgr_ui.c
 * @brief  任务管理器 UI 渲染实现
 * @details 纯渲染函数：标题栏、任务列表、确认弹窗、底部信息栏。
 *          不包含任何业务逻辑或 HAL 生命周期调用。
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr_ui.h"
#include "taskmgr.h"
#include "kernel/kern_task.h"
#include "kernel/kern_types.h"
#include "hal/hal_display.h"
#include <stdio.h>

/* ═══ 常量 ═══ */

#define LINE_HEIGHT  14
#define HEADER_Y      2
#define LIST_Y       18

/* ═══ 前向声明 ═══ */

int taskmgr_visible_lines(void);

/* ═══ 渲染辅助 ═══ */

static const char *state_str(kern_task_state_t state)
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

/* ═══ 各区域绘制 ═══ */

/**
 * @brief 绘制标题栏
 */
static void draw_header(void)
{
    hal_draw_string(2, HEADER_Y, "Task Manager", COLOR_ACCENT);
    hal_draw_line(0, LIST_Y - 2, SCREEN_WIDTH, LIST_Y - 2, COLOR_ACCENT);
}

/**
 * @brief 绘制任务列表
 */
static void draw_list(void)
{
    int visible = taskmgr_visible_lines();
    int count = taskmgr_get_count();
    int selected = taskmgr_get_selected();
    int scroll = taskmgr_get_scroll();

    for (int i = 0; i < visible && (i + scroll) < count; i++) {
        int idx = i + scroll;
        kern_task_t *t = taskmgr_get_task(idx);
        if (t == NULL) continue;

        int y = LIST_Y + i * LINE_HEIGHT;
        bool is_sel = (idx == selected);
        bool is_prot = taskmgr_is_task_protected(idx);
        uint16_t color = is_prot ? COLOR_ACCENT : COLOR_FG;

        /* 选中行高亮 */
        if (is_sel) {
            hal_draw_fill_rect(0, y, SCREEN_WIDTH, (int16_t)LINE_HEIGHT, COLOR_FG);
            color = COLOR_BG;
        }

        char line[48];
        if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
            snprintf(line, sizeof(line), "%2d %-12s %s  n/a",
                     t->pid, t->name, state_str(t->state));
        } else {
            snprintf(line, sizeof(line), "%2d %-12s %s  %zu",
                     t->pid, t->name, state_str(t->state),
                     kern_task_stack_usage(t));
        }
        hal_draw_string(2, y, line, color);
    }
}

/**
 * @brief 绘制确认弹窗提示（选中行上方）
 */
static void draw_confirm_overlay(void)
{
    if (!taskmgr_is_confirming()) return;

    kern_task_t *t = taskmgr_get_task(taskmgr_get_selected());
    if (t == NULL) return;

    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2 - 10;

    hal_draw_fill_rect((int16_t)(cx - 62), (int16_t)(cy - 10), 124, 28, COLOR_BG);
    hal_draw_rect((int16_t)(cx - 62), (int16_t)(cy - 10), 124, 28, COLOR_RED);

    char msg[40];
    snprintf(msg, sizeof(msg), "Kill %s?", t->name);
    hal_draw_string((int16_t)(cx - 56), (int16_t)(cy - 2), msg, COLOR_RED);

    hal_draw_string((int16_t)(cx - 56), (int16_t)(cy + 10),
                    "Hold A:Yes B:No", COLOR_FG);
}

/**
 * @brief 绘制底部信息栏
 */
static void draw_footer(void)
{
    int footer_y = SCREEN_HEIGHT - 16;
    int selected = taskmgr_get_selected();

    kern_task_t *t = taskmgr_get_task(selected);
    if (t == NULL) return;

    char info[64];
    bool is_prot = taskmgr_is_task_protected(selected);

    if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
        snprintf(info, sizeof(info), "PID:%d [V] %s",
                 t->pid, is_prot ? "PROTECTED" : "killable");
    } else {
        snprintf(info, sizeof(info), "PID:%d %s stk:%zu/%zu %s",
                 t->pid, t->name,
                 kern_task_stack_usage(t), t->stack_size,
                 is_prot ? "PROTECTED" : "killable");
    }
    hal_draw_string(2, footer_y, info, COLOR_ACCENT);
}

/* ═══ 入口 ═══ */

/**
 * @brief 绘制完整任务管理器界面
 */
void taskmgr_draw(void)
{
    draw_header();
    draw_list();
    draw_confirm_overlay();
    draw_footer();
}
