/**
 * @file   taskmgr_ui.c
 * @brief  任务管理器 UI 渲染实现
 * @details 纯渲染函数：标题栏、任务列表、确认弹窗、底部信息栏。
 *          遵循 Xerintosh UI 框架规范：使用 hal_get_font_height()
 *          计算行高，baseline 对齐，标准颜色常量。
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr_ui.h"
#include "taskmgr.h"
#include "kernel/kern_task.h"
#include "hal/hal_display.h"
#include <stdio.h>

/* ═══ 布局常量（对齐框架 ui_item.h 规范）═══ */

#define LEFT_MARGIN     4    /* 左边缘 */
#define HEADER_Y         2    /* 标题栏 Y */
#define FOOTER_MARGIN    4    /* 底部信息栏距底部距离 */

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

/**
 * @brief 格式化为单行显示字符串
 */
static void format_line(kern_task_t *task, char *buf, size_t size)
{
    if (task == NULL || buf == NULL || size == 0) return;

    bool is_prot = kern_task_is_protected(task);
    const char *mark = is_prot ? "*" : " ";

    if (task->flags & KERN_TASK_FLAG_VIRTUAL) {
        snprintf(buf, size, "%s%2d %-12s %s  n/a",
                 mark, task->pid, task->name,
                 state_str(task->state));
    } else {
        snprintf(buf, size, "%s%2d %-12s %s  %zu",
                 mark, task->pid, task->name,
                 state_str(task->state),
                 kern_task_stack_usage(task));
    }
}

/* ═══ 各区域绘制 ═══ */

/**
 * @brief 绘制标题栏
 * @note  使用 hal_get_font_height() 计算行高，避免硬编码值导致重叠
 */
static void draw_header(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    hal_draw_string(LEFT_MARGIN, HEADER_Y + fh, "Task Manager", COLOR_ACCENT);

    /* 分隔线 */
    int16_t sep_y = HEADER_Y + fh + 3;
    hal_draw_line(0, sep_y, SCREEN_WIDTH, sep_y, COLOR_ACCENT);
}

/**
 * @brief 绘制任务列表
 */
static void draw_list(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    /* 列表区域顶部：标题栏下方 */
    int16_t list_top = HEADER_Y + fh + 6;

    int count = taskmgr_get_count();
    int selected = taskmgr_get_selected();
    int scroll = taskmgr_get_scroll();
    int visible = taskmgr_visible_lines();

    for (int i = 0; i < visible && (i + scroll) < count; i++) {
        int idx = i + scroll;
        kern_task_t *t = taskmgr_get_task(idx);
        if (t == NULL) continue;

        /* 行顶部 Y */
        int16_t row_y = list_top + (int16_t)(i * (fh + 4));
        /* 文字基线 Y（hal_draw_string 使用 baseline_left 基准） */
        int16_t text_y = row_y + fh;

        bool is_sel = (idx == selected);
        bool is_prot = taskmgr_is_task_protected(idx);

        /* 选中行：白底黑字反色 */
        if (is_sel) {
            hal_draw_fill_rect(0, row_y, SCREEN_WIDTH, (int16_t)(fh + 4), COLOR_FG);
            char line[48];
            format_line(t, line, sizeof(line));
            hal_draw_string(LEFT_MARGIN + 1, text_y, line, COLOR_BG);
        } else {
            uint16_t text_color = is_prot ? COLOR_ACCENT : COLOR_FG;
            char line[48];
            format_line(t, line, sizeof(line));
            hal_draw_string(LEFT_MARGIN + 1, text_y, line, text_color);
        }
    }
}

/**
 * @brief 绘制确认弹窗提示
 */
static void draw_confirm_overlay(void)
{
    if (!taskmgr_is_confirming()) return;

    kern_task_t *t = taskmgr_get_task(taskmgr_get_selected());
    if (t == NULL) return;

    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    int16_t cx = SCREEN_WIDTH / 2;
    int16_t cy = SCREEN_HEIGHT / 2 - 12;
    int16_t bw = 130;
    int16_t bh = (int16_t)(fh * 2 + 10);

    hal_draw_fill_rect((int16_t)(cx - bw / 2), cy, bw, bh, COLOR_BG);
    hal_draw_rect((int16_t)(cx - bw / 2), cy, bw, bh, COLOR_ACCENT);

    char msg[40];
    snprintf(msg, sizeof(msg), "Kill '%s'?", t->name);
    hal_draw_string((int16_t)(cx - 60), cy + fh, msg, COLOR_ACCENT);

    hal_draw_string((int16_t)(cx - 60), cy + fh * 2 + 2,
                    "A=Yes  B=No", COLOR_FG);
}

/**
 * @brief 绘制底部信息栏
 */
static void draw_footer(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    int selected = taskmgr_get_selected();
    kern_task_t *t = taskmgr_get_task(selected);
    if (t == NULL) return;

    int16_t footer_y = SCREEN_HEIGHT - fh - FOOTER_MARGIN;

    /* 分隔线 */
    hal_draw_line(0, footer_y - 2, SCREEN_WIDTH, footer_y - 2, COLOR_ACCENT);

    bool is_prot = taskmgr_is_task_protected(selected);
    char info[64];

    if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
        snprintf(info, sizeof(info), "PID:%d [V] %s",
                 t->pid, is_prot ? "PROTECTED" : "killable");
    } else {
        snprintf(info, sizeof(info), "PID:%d %s  stk:%zu/%zu  %s",
                 t->pid, t->name,
                 kern_task_stack_usage(t), t->stack_size,
                 is_prot ? "PROTECTED" : "killable");
    }
    hal_draw_string(LEFT_MARGIN, footer_y + fh, info, COLOR_ACCENT);
}

/* ═══ 入口 ═══ */

void taskmgr_draw(void)
{
    draw_header();
    draw_list();
    draw_confirm_overlay();
    draw_footer();
}
