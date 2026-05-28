/**
 * @file   taskmgr_ui.c
 * @brief  任务管理器 UI 渲染实现
 * @details 纯渲染函数：标题栏、任务列表、确认弹窗、底部信息栏。
 *          遵循 Xerintosh UI 框架规范：使用 hal_get_font_height()
 *          计算行高，baseline 对齐，标准颜色常量。
 *          Phase 1: 集成 ui_anim_row 动画坐标渲染。
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr_ui.h"
#include "taskmgr.h"
#include "kernel/kern_task.h"
#include "hal/hal_display.h"
#include "ui/ui_anim_row.h"
#include <stdio.h>

/* ═══ 布局常量（对齐框架 ui_item.h 规范，与 taskmgr.h 共享）═══ */

/* TASKMGR_TASKMGR_HEADER_Y=2, TASKMGR_TASKMGR_LEFT_MARGIN=4 (defined in taskmgr.h) */
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
 */
static void draw_header(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    hal_draw_string(TASKMGR_LEFT_MARGIN, TASKMGR_HEADER_Y + fh, "Task Manager", COLOR_FG);

    /* 分隔线（缩减间距：+3 替代 +6） */
    int16_t sep_y = TASKMGR_HEADER_Y + fh + 3;
    hal_draw_line(0, sep_y, SCREEN_WIDTH, sep_y, COLOR_FG);
}

/**
 * @brief 绘制任务列表（使用动画坐标）
 * @note  纯黑白配色：白底黑字（选中），黑底白字（未选中）。
 *         受保护任务以 "*" 前缀标识。
 *         使用 ui_anim_row 动画坐标实现平滑的入场和切换效果。
 */
static void draw_list(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    const xerintosh_anim_row_list_t *anim = taskmgr_get_anim_list();

    int count = taskmgr_get_count();
    int selected = taskmgr_get_selected();
    int scroll = taskmgr_get_scroll();
    int visible = taskmgr_visible_lines();

    for (int i = 0; i < visible && (i + scroll) < count; i++) {
        int idx = i + scroll;
        kern_task_t *t = taskmgr_get_task(idx);
        if (t == NULL) continue;

        /* 使用动画 Y 坐标替代整数计算 */
        int16_t row_y = (int16_t)anim->rows[i].y;
        int16_t text_y = row_y + fh;
        bool is_sel = (idx == selected);

        char line[48];
        format_line(t, line, sizeof(line));

        if (is_sel) {
            /* 选中行：使用动画高亮框 */
            int16_t hy = (int16_t)anim->highlight.y;
            int16_t hw = (int16_t)anim->highlight.w;
            hal_draw_fill_rect(0, hy, hw, (int16_t)(fh + 4), COLOR_FG);
            hal_draw_string(TASKMGR_LEFT_MARGIN + 1, text_y, line, COLOR_BG);
        } else {
            /* 未选中行：黑底白字 */
            hal_draw_string(TASKMGR_LEFT_MARGIN + 1, text_y, line, COLOR_FG);
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
    hal_draw_rect((int16_t)(cx - bw / 2), cy, bw, bh, COLOR_FG);

    char msg[40];
    snprintf(msg, sizeof(msg), "Kill '%s'?", t->name);
    hal_draw_string((int16_t)(cx - 60), cy + fh, msg, COLOR_FG);

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

    int16_t footer_baseline = SCREEN_HEIGHT - 2;
    int16_t footer_y        = footer_baseline - fh;

    /* 分隔线 */
    hal_draw_line(0, footer_y - 2, SCREEN_WIDTH, footer_y - 2, COLOR_FG);

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
    hal_draw_string(TASKMGR_LEFT_MARGIN, footer_y + fh, info, COLOR_FG);
}

/* ═══ 入口 ═══ */

void taskmgr_draw(void)
{
    draw_header();
    draw_list();
    draw_confirm_overlay();
    draw_footer();
}
