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
#include "hal/hal_layout.h"
#include "ui/ui_anim_row.h"
#include "ui/ui_drawer.h"
#include "hal/hal_system.h"
#include <stdio.h>
#include <string.h>

/* ═══ 布局常量（对齐框架 ui_item.h 规范）═══ */

/* 布局常量已统一迁移至 taskmgr.h 和 hal_layout.h — 此处不再定义 */

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
 * @brief 将字节数格式化为紧凑的 B/K/M 单位字符串
 */
static void format_size(size_t bytes, char *buf, size_t size)
{
    if (buf == NULL || size == 0) return;

    if (bytes < 1024) {
        snprintf(buf, size, "%zuB", bytes);
    } else if (bytes < 1024 * 1024) {
        size_t kb = bytes / 1024;
        size_t frac = (bytes % 1024) * 10 / 1024;
        snprintf(buf, size, "%zu.%zuK", kb, frac);
    } else {
        size_t mb = bytes / (1024 * 1024);
        size_t frac = (bytes % (1024 * 1024)) * 10 / (1024 * 1024);
        snprintf(buf, size, "%zu.%zuM", mb, frac);
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
    char size_str[16];

    if (task->flags & KERN_TASK_FLAG_VIRTUAL) {
        snprintf(buf, size, "%s%2d %-12s %s  n/a",
                 mark, task->pid, task->name,
                 state_str(task->state));
    } else {
        format_size(kern_task_stack_usage(task), size_str, sizeof(size_str));
        snprintf(buf, size, "%s%2d %-12s %s  %s",
                 mark, task->pid, task->name,
                 state_str(task->state),
                 size_str);
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
    int16_t title_y = HAL_TEXT_BASELINE(HAL_HEADER_TOP()) - 2;  /* 微调上移 */
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

    hal_clear_clip_rect();
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

    int16_t bw = (int16_t)(SCREEN_WIDTH * 7 / 8);   /* 宽度比例自适应 */
    int16_t bh = (int16_t)(fh * 2 + HAL_MARGIN_LG);  /* 两行文字 + 内边距 */
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

/* ═══ 底部信息栏跑马灯状态 ═══ */

static char     s_footer_info[64];
static int16_t  s_footer_text_width;
static int16_t  s_footer_avail_width;
static bool     s_footer_is_scrolling;
static uint32_t s_footer_scroll_start_ms;

/**
 * @brief 更新底部信息栏文本与滚动状态
 * @note  当文本宽度超过可用宽度时启用跑马灯滚动。
 */
static void update_footer_info(kern_task_t *t, bool is_prot)
{
    if (t->flags & KERN_TASK_FLAG_VIRTUAL) {
        snprintf(s_footer_info, sizeof(s_footer_info), "PID:%d [V] %s",
                 t->pid, is_prot ? "PROTECTED" : "killable");
    } else {
        char used_str[16];
        char total_str[16];
        format_size(kern_task_stack_usage(t), used_str, sizeof(used_str));
        format_size(t->stack_size, total_str, sizeof(total_str));
        snprintf(s_footer_info, sizeof(s_footer_info), "PID:%d %s  stk:%s/%s  %s",
                 t->pid, t->name,
                 used_str, total_str,
                 is_prot ? "PROTECTED" : "killable");
    }

    s_footer_text_width = hal_get_string_width(s_footer_info);
    s_footer_avail_width = SCREEN_WIDTH - TASKMGR_LEFT_MARGIN - HAL_MARGIN_MD;

    bool need_scroll = (s_footer_text_width > s_footer_avail_width);
    if (need_scroll && !s_footer_is_scrolling) {
        s_footer_is_scrolling = true;
        s_footer_scroll_start_ms = hal_get_ticks();
    } else if (!need_scroll) {
        s_footer_is_scrolling = false;
    }
}

/**
 * @brief 绘制底部信息栏
 * @note  信息过长时采用与菜单项相同的无缝循环跑马灯效果。
 */
static void draw_footer(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    int selected = taskmgr_get_selected();
    kern_task_t *t = taskmgr_get_task(selected);
    if (t == NULL) return;

    /* 分隔线 */
    int16_t sep_y = HAL_FOOTER_TOP();
    hal_draw_line(0, sep_y, SCREEN_WIDTH, sep_y, COLOR_FG);

    bool is_prot = taskmgr_is_task_protected(selected);
    update_footer_info(t, is_prot);

    int16_t text_y = HAL_FOOTER_BOTTOM() - HAL_MARGIN_SM;  /* 底部留边距 */
    float scroll_x = 0.0f;

    if (s_footer_is_scrolling) {
        uint32_t elapsed = hal_get_ticks() - s_footer_scroll_start_ms;
        scroll_x = xerintosh_compute_scroll_offset(s_footer_text_width,
                                                   s_footer_avail_width,
                                                   true, elapsed);
    }

    /* 设置裁剪区域，限制文字只在 footer 行内显示 */
    int16_t clip_y = HAL_FOOTER_TOP();
    hal_set_clip_rect(0, clip_y, SCREEN_WIDTH, HAL_ROW_H());

    int16_t draw_x = TASKMGR_LEFT_MARGIN - (int16_t)scroll_x;
    int16_t cycle_dist = s_footer_text_width + s_footer_avail_width;

    /* 绘制两份文字形成无缝循环 */
    hal_draw_string(draw_x, text_y, s_footer_info, COLOR_FG);
    if (s_footer_is_scrolling) {
        hal_draw_string(draw_x + cycle_dist, text_y, s_footer_info, COLOR_FG);
    }

    hal_clear_clip_rect();
}

/* ═══ 入口 ═══ */

void taskmgr_draw(void)
{
    draw_header();
    draw_list();
    draw_confirm_overlay();
    draw_footer();
}
