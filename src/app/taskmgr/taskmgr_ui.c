/**
 * @file   taskmgr_ui.c
 * @brief  任务管理器 UI 渲染实现
 * @details 纯渲染函数：标题栏、任务列表、确认弹窗、底部信息栏。
 *          使用 taskmgr_task_info_t 替代原 Xeros kern_task_t。
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr_ui.h"
#include "taskmgr.h"
#include "hal/hal_display.h"
#include "hal/hal_layout.h"
#include "ui/ui_anim_row.h"
#include <stdio.h>

/* ═══ 布局常量已统一迁移至 taskmgr.h 和 hal_layout.h — 此处不再定义 ═══ */

/* ═══ 渲染辅助 ═══ */

/**
 * @brief 格式化为单行显示字符串
 */
static void format_line(const taskmgr_task_info_t *task, char *buf, size_t size)
{
    if (task == NULL || buf == NULL || size == 0) return;

    const char *mark = task->is_protected ? "*" : " ";

    if (task->is_virtual) {
        snprintf(buf, size, "%s%2d %-12s %s  n/a",
                 mark, task->index, task->name, task->state_str);
    } else {
        snprintf(buf, size, "%s%2d %-12s %s  %d",
                 mark, task->index, task->name, task->state_str,
                 task->stack_free);
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

    /* 标题在 header 行内水平居中 */
    int16_t title_w = hal_get_string_width("Task Manager");
    int16_t title_x = HAL_CENTER_X(title_w);
    int16_t title_y = HAL_TEXT_BASELINE(HAL_HEADER_TOP()) - 2;
    hal_draw_string(title_x, title_y, "Task Manager", COLOR_FG);

    /* 分隔线 */
    int16_t sep_y = HAL_HEADER_BOTTOM();
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

    /* 裁剪列表渲染区域，防止动画行溢出到 header/footer */
    int16_t clip_h = (int16_t)(anim->visible_count * anim->row_height);
    hal_set_clip_rect(0, anim->list_top, SCREEN_WIDTH, clip_h);

    for (int i = 0; i < visible && (i + scroll) < count; i++) {
        int idx = i + scroll;
        const taskmgr_task_info_t *t = taskmgr_get_task(idx);
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

    hal_clear_clip_rect();
}

/**
 * @brief 绘制确认弹窗提示
 */
static void draw_confirm_overlay(void)
{
    if (!taskmgr_is_confirming()) return;

    const taskmgr_task_info_t *t = taskmgr_get_task(taskmgr_get_selected());
    if (t == NULL) return;

    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    int16_t bw = (int16_t)(SCREEN_WIDTH * 7 / 8);
    int16_t bh = (int16_t)(fh * 2 + HAL_MARGIN_LG);
    int16_t bx = HAL_CENTER_X(bw);
    int16_t by = HAL_CENTER_Y(bh);

    hal_draw_fill_rect(bx, by, bw, bh, COLOR_BG);
    hal_draw_rect(bx, by, bw, bh, COLOR_FG);

    char msg[40];
    snprintf(msg, sizeof(msg), "Kill '%s'?", t->name);
    int16_t msg_w = hal_get_string_width(msg);
    hal_draw_string(HAL_CENTER_X(msg_w), by + fh, msg, COLOR_FG);

    int16_t hint_w = hal_get_string_width("A=Yes  B=No");
    hal_draw_string(HAL_CENTER_X(hint_w), by + fh * 2 + HAL_MARGIN_SM,
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
    const taskmgr_task_info_t *t = taskmgr_get_task(selected);
    if (t == NULL) return;

    /* 分隔线 */
    int16_t sep_y = HAL_FOOTER_TOP();
    hal_draw_line(0, sep_y, SCREEN_WIDTH, sep_y, COLOR_FG);

    bool is_prot = taskmgr_is_task_protected(selected);
    char info[64];

    if (t->is_virtual) {
        snprintf(info, sizeof(info), "#%d [V] %s",
                 t->index, is_prot ? "PROTECTED" : "killable");
    } else {
        snprintf(info, sizeof(info), "#%d %s  free:%d  %s",
                 t->index, t->name,
                 t->stack_free,
                 is_prot ? "PROTECTED" : "killable");
    }
    int16_t text_y = HAL_FOOTER_BOTTOM() - HAL_MARGIN_SM;
    hal_draw_string(TASKMGR_LEFT_MARGIN, text_y, info, COLOR_FG);
}

/* ═══ 入口 ═══ */

void taskmgr_draw(void)
{
    draw_header();
    draw_list();
    draw_confirm_overlay();
    draw_footer();
}
