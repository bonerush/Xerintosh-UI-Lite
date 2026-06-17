/**
 * @file   taskmgr_ui.c
 * @brief  任务管理器 UI 渲染实现
 * @details 列式布局：标记/PID/名称框(滚动)/状态+栈(右对齐)。
 *          名称框动态填充 PID 右端到状态+栈左端。
 *          全 5 字符状态名（READY / RUN / SLEEP / BLOCK / ZOMBI）。
 *          "统计信息+栈使用量" 合并为右对齐文本块。
 *          选中行名称超长时无缝跑马灯滚动。
 *
 *          布局（80px 宽度示例，字体 6×8）：
 *          ┌─┬──┬────────────────┬───────────┐
 *          │*│12│ name→scroll    │READY 128K │
 *          └─┴──┴────────────────┴───────────┘
 *          mark(6) PID(14) 名称(动态)  状态+栈(右对齐)
 *
 * @copyright Copyright (c) 2026
 */

#include "taskmgr_ui.h"
#include "taskmgr.h"
#include "hal/hal_display.h"
#include "hal/hal_layout.h"
#include "ui/ui_anim_row.h"
#include "ui/ui_drawer.h"
#include "hal/hal_system.h"
#include <stdio.h>
#include <string.h>

/* ═══ 列布局常量 ═══ */

/* 文字内容起始 X */
#define TM_X0           ((int16_t)(TASKMGR_LEFT_MARGIN + 1))  /* = 5 */
#define TM_RIGHT_MARGIN TM_X0   /* 距右边缘间距，与左侧对齐一致 */
#define TM_GAP          2       /* 默认列间间距 */
#define TM_GAP_PID_NAME 3       /* PID → 名称框，再宽一点 */
#define TM_W_MARK       6       /* 保护标记："*" 或 " " */
#define TM_W_PID        14      /* PID：右对齐 */

#define TM_X_MARK       TM_X0
#define TM_X_PID        (TM_X_MARK + TM_W_MARK + TM_GAP)

/* ═══ 名称框滚动状态 ═══ */

typedef struct {
    bool     active;
    uint32_t start_ms;
    int      task_index;
} tm_scroll_t;

static tm_scroll_t g_name_scroll = {false, 0, -1};

/* ═══ 渲染辅助 ═══ */

/**
 * @brief 状态全名（5 字符）
 */
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
 * @brief 紧凑栈大小格式（≤4 字符）
 * @param bytes 栈使用量（字节）
 * @param buf    输出缓冲区（至少 8 字节）
 * @param size   缓冲区大小
 */
static void format_stack_compact(size_t bytes, char *buf, size_t size)
{
    if (buf == NULL || size == 0) return;
    if (bytes < 1024) {
        snprintf(buf, size, "%zuB", bytes);      /* "0B" ~ "1023B" */
    } else if (bytes < 1024 * 1024) {
        size_t kb = (bytes + 512) / 1024;         /* 四舍五入到 KB */
        if (kb >= 1000) { snprintf(buf, size, ">K"); return; }
        snprintf(buf, size, "%zuK", kb);          /* "1K" ~ "999K" */
    } else {
        size_t mb = bytes / (1024 * 1024);
        if (mb >= 100) { snprintf(buf, size, ">M"); return; }
        snprintf(buf, size, "%zuM", mb);          /* "1M" ~ "99M" */
    }
}

/**
 * @brief 将字节数格式化为 B/K/M 单位字符串（供 footer 使用）
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
 * @brief 格式化为右对齐文本块（"状态 栈"）
 * @param t    任务指针
 * @param buf  输出缓冲区
 * @param size 缓冲区大小
 */
static void format_right_text(kern_task_t *t, char *buf, size_t size)
{
    if (t == NULL || buf == NULL || size == 0) return;
    const char *state = state_str(t->state);
    if (taskmgr_task_is_virtual(t)) {
        snprintf(buf, size, "%s --", state);
    } else {
        char stack_buf[8];
        format_stack_compact(taskmgr_task_stack_usage(t), stack_buf, sizeof(stack_buf));
        snprintf(buf, size, "%s %s", state, stack_buf);
    }
}

/* ═══ 标题栏 ═══ */

static void draw_header(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    int16_t title_w = hal_get_string_width("Task Manager");
    int16_t title_x = HAL_CENTER_X(title_w);
    int16_t title_y = HAL_TEXT_BASELINE(HAL_HEADER_TOP()) - 2;
    hal_draw_string(title_x, title_y, "Task Manager", COLOR_FG);

    int16_t sep_y = HAL_HEADER_BOTTOM();
    hal_draw_line(0, sep_y, SCREEN_WIDTH, sep_y, COLOR_FG);
}

/* ═══ 任务列表 ═══ */

static void draw_list(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    const xerintosh_anim_row_list_t *anim = taskmgr_get_anim_list();

    int count = taskmgr_get_count();
    int selected = taskmgr_get_selected();
    int scroll = taskmgr_get_scroll();
    int visible = taskmgr_visible_lines();

    int16_t list_top = anim->list_top;
    int16_t list_bottom = HAL_FOOTER_TOP();

    for (int i = 0; i < visible && (i + scroll) < count; i++) {
        int idx = i + scroll;
        kern_task_t *t = taskmgr_get_task(idx);
        if (t == NULL) continue;

        int16_t row_y = (int16_t)anim->rows[i].y;
        int16_t text_y = row_y + fh;
        bool is_sel = (idx == selected);
        bool is_prot = taskmgr_task_protected(t);

        /* 跳过动画入场/出场过程中不可见的行 */
        if (row_y + fh + 4 < list_top || row_y > list_bottom)
            continue;

        /* ── 选中行：反色高亮框 ── */
        if (is_sel) {
            int16_t hy = (int16_t)anim->highlight.y;
            int16_t hw = (int16_t)anim->highlight.w;
            hal_draw_fill_rect(0, hy, hw, (int16_t)(fh + 4), COLOR_FG);
        }

        uint16_t fg = is_sel ? COLOR_BG : COLOR_FG;

        /* ── 第1列：保护标记 ── */
        hal_draw_string(TM_X_MARK, text_y, is_prot ? "*" : " ", fg);

        /* ── 第2列：PID（右对齐） ── */
        {
            char pid_str[8];
            snprintf(pid_str, sizeof(pid_str), "%d", (int)t->pid);
            int16_t pid_w = hal_get_string_width(pid_str);
            int16_t pid_x = TM_X_PID + (TM_W_PID - pid_w);
            if (pid_x < TM_X_PID) pid_x = TM_X_PID;
            hal_draw_string(pid_x, text_y, pid_str, fg);
        }

        /* ── 右侧文本块：状态 + 栈（右对齐，每行独立计算） ── */
        int16_t x_right = 0;
        int16_t w_right = 0;
        {
            char right_buf[20];
            format_right_text(t, right_buf, sizeof(right_buf));
            w_right = hal_get_string_width(right_buf);
            x_right = (int16_t)(SCREEN_WIDTH - TM_RIGHT_MARGIN - w_right);

            /* 裁剪：防止右对齐文本溢出屏幕左/右边缘 */
            hal_set_clip_rect(x_right, row_y, w_right, (int16_t)(fh + 4));
            hal_draw_string(x_right, text_y, right_buf, fg);
            hal_clear_clip_rect();
        }

        /* ── 第3列：名称框（填充 PID 到右侧文本块之间的空间） ── */
        {
            const char *name = t->name;
            int16_t name_w = hal_get_string_width(name);

            int16_t x_name = TM_X_PID + TM_W_PID + TM_GAP_PID_NAME;
            int16_t w_name = x_right - TM_GAP - x_name;
            if (w_name < 6) w_name = 6;

            float scroll_x = 0.0f;

            bool need_scroll = (is_sel && name_w > w_name);
            if (need_scroll) {
                if (!g_name_scroll.active || g_name_scroll.task_index != idx) {
                    g_name_scroll.active = true;
                    g_name_scroll.start_ms = hal_get_ticks();
                    g_name_scroll.task_index = idx;
                }
                uint32_t elapsed = hal_get_ticks() - g_name_scroll.start_ms;
                scroll_x = xerintosh_compute_scroll_offset(name_w, w_name, true, elapsed);
            } else if (g_name_scroll.task_index == idx) {
                g_name_scroll.active = false;
            }

            hal_set_clip_rect(x_name, row_y, w_name, (int16_t)(fh + 4));

            int16_t cycle_dist = name_w + w_name;
            int16_t draw_x = x_name - (int16_t)scroll_x;

            hal_draw_string(draw_x, text_y, name, fg);
            if (need_scroll) {
                hal_draw_string(draw_x + cycle_dist, text_y, name, fg);
            }

            hal_clear_clip_rect();
        }
    }
}

/* ═══ 确认弹窗 ═══ */

static void draw_confirm_overlay(void)
{
    if (!taskmgr_is_confirming()) return;

    kern_task_t *t = taskmgr_get_task(taskmgr_get_selected());
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

/* ═══ 底部信息栏跑马灯 ═══ */

static char     s_footer_info[64];
static int16_t  s_footer_text_width;
static int16_t  s_footer_avail_width;
static bool     s_footer_is_scrolling;
static uint32_t s_footer_scroll_start_ms;

static void update_footer_info(kern_task_t *t, bool is_prot)
{
    if (taskmgr_task_is_virtual(t)) {
        snprintf(s_footer_info, sizeof(s_footer_info), "PID:%d [V] %s",
                 t->pid, is_prot ? "PROTECTED" : "killable");
    } else {
        char used_str[16];
        char total_str[16];
        format_size(taskmgr_task_stack_usage(t), used_str, sizeof(used_str));
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

static void draw_footer(void)
{
    int16_t fh = hal_get_font_height();
    hal_set_font(hal_get_cn_font());

    int selected = taskmgr_get_selected();
    kern_task_t *t = taskmgr_get_task(selected);
    if (t == NULL) return;

    int16_t sep_y = HAL_FOOTER_TOP();
    hal_draw_line(0, sep_y, SCREEN_WIDTH, sep_y, COLOR_FG);

    bool is_prot = taskmgr_is_task_protected(selected);
    update_footer_info(t, is_prot);

    int16_t text_y = HAL_FOOTER_BOTTOM() - HAL_MARGIN_SM;
    float scroll_x = 0.0f;

    if (s_footer_is_scrolling) {
        uint32_t elapsed = hal_get_ticks() - s_footer_scroll_start_ms;
        scroll_x = xerintosh_compute_scroll_offset(s_footer_text_width,
                                                   s_footer_avail_width,
                                                   true, elapsed);
    }

    int16_t clip_y = HAL_FOOTER_TOP();
    hal_set_clip_rect(0, clip_y, SCREEN_WIDTH, HAL_ROW_H());

    int16_t draw_x = TASKMGR_LEFT_MARGIN - (int16_t)scroll_x;
    int16_t cycle_dist = s_footer_text_width + s_footer_avail_width;

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
