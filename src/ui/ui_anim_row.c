/**
 * @file   ui_anim_row.c
 * @brief  公共行列表动画工具实现
 * @details 提供行列表动画的初始化、逐帧更新、目标刷新三个核心操作。
 *          逐帧更新中根据 g_spring_anim_mode 直接分派到
 *          xerintosh_spring_animation() 或 xerintosh_animation()，
 *          不再经过 xerintosh_animate_unified() 中间层。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_anim_row.h"
#include "ui_core.h"
#include "ui_types.h"   /* g_spring_anim_mode, g_spring_stiffness_selector, g_spring_damping_selector */
#include "hal/hal_display.h"
#include <math.h>

/* ═══ 初始化 ═══ */

void xerintosh_anim_row_list_init(xerintosh_anim_row_list_t *list,
                                   int visible_count, int16_t row_height,
                                   int16_t list_top)
{
    list->visible_count = visible_count;
    list->row_height = row_height;
    list->list_top = list_top;

    for (int i = 0; i < visible_count && i < ANIM_ROW_MAX; i++) {
        list->rows[i].y = (float)HAL_SCREEN_HEIGHT;  /* 入场起点：屏幕底部 */
        list->rows[i].y_trg = (float)(list_top + i * row_height);
        list->rows[i].w = (float)HAL_SCREEN_WIDTH;
        list->rows[i].w_trg = (float)HAL_SCREEN_WIDTH;
        list->rows[i].v_y = 0.0f;
        list->rows[i].v_w = 0.0f;
    }

    list->highlight.y = (float)HAL_SCREEN_HEIGHT;
    list->highlight.y_trg = (float)list_top;
    list->highlight.w = (float)HAL_SCREEN_WIDTH;
    list->highlight.w_trg = (float)HAL_SCREEN_WIDTH;
    list->highlight.v_y = 0.0f;
    list->highlight.v_w = 0.0f;

    list->scroll_offset = 0.0f;
    list->scroll_offset_trg = 0.0f;
    list->v_scroll_offset = 0.0f;
}

/* ═══ 逐帧更新 ═══ */

bool xerintosh_anim_row_list_update(xerintosh_anim_row_list_t *list, float speed)
{
    bool all_settled = true;
    const bool use_spring = g_spring_anim_mode;

    for (int i = 0; i < list->visible_count && i < ANIM_ROW_MAX; i++) {
        if (use_spring) {
            xerintosh_spring_animation(&list->rows[i].y, &list->rows[i].v_y,
                                        list->rows[i].y_trg,
                                        g_spring_stiffness_selector,
                                        g_spring_damping_selector);
            xerintosh_spring_animation(&list->rows[i].w, &list->rows[i].v_w,
                                        list->rows[i].w_trg,
                                        g_spring_stiffness_selector,
                                        g_spring_damping_selector);
        } else {
            xerintosh_animation(&list->rows[i].y, list->rows[i].y_trg, speed);
            xerintosh_animation(&list->rows[i].w, list->rows[i].w_trg, speed);
        }
        /* 同时检查 y 和 w 两个维度的稳定状态 */
        if (fabsf(list->rows[i].y - list->rows[i].y_trg) > 0.5f
            || fabsf(list->rows[i].w - list->rows[i].w_trg) > 0.5f)
            all_settled = false;
    }

    if (use_spring) {
        xerintosh_spring_animation(&list->highlight.y, &list->highlight.v_y,
                                    list->highlight.y_trg,
                                    g_spring_stiffness_selector,
                                    g_spring_damping_selector);
        xerintosh_spring_animation(&list->highlight.w, &list->highlight.v_w,
                                    list->highlight.w_trg,
                                    g_spring_stiffness_selector,
                                    g_spring_damping_selector);
    } else {
        xerintosh_animation(&list->highlight.y, list->highlight.y_trg, speed);
        xerintosh_animation(&list->highlight.w, list->highlight.w_trg, speed);
    }
    if (fabsf(list->highlight.y - list->highlight.y_trg) > 0.5f
        || fabsf(list->highlight.w - list->highlight.w_trg) > 0.5f)
        all_settled = false;

    if (use_spring) {
        xerintosh_spring_animation(&list->scroll_offset, &list->v_scroll_offset,
                                    list->scroll_offset_trg,
                                    g_spring_stiffness_selector,
                                    g_spring_damping_selector);
    } else {
        xerintosh_animation(&list->scroll_offset, list->scroll_offset_trg, speed);
    }
    if (fabsf(list->scroll_offset - list->scroll_offset_trg) > 0.5f)
        all_settled = false;

    return all_settled;
}

/* ═══ 目标刷新 ═══ */

void xerintosh_anim_row_list_refresh(xerintosh_anim_row_list_t *list,
                                      int selected, int scroll,
                                      int16_t screen_width, int item_count)
{
    for (int i = 0; i < list->visible_count && i < ANIM_ROW_MAX; i++) {
        int idx = i + scroll;
        if (idx < item_count) {
            list->rows[i].y_trg = (float)(list->list_top + i * list->row_height);
            list->rows[i].w_trg = (float)screen_width;
        } else {
            /* 越界项移出视野 */
            list->rows[i].y_trg = (float)(list->list_top + (i + 1) * list->row_height);
            list->rows[i].w_trg = (float)screen_width;
        }
    }

    /* 高亮框跟踪选中行 */
    int sel_visible = selected - scroll;
    if (sel_visible >= 0 && sel_visible < list->visible_count) {
        list->highlight.y_trg = list->rows[sel_visible].y_trg;
        list->highlight.w_trg = (float)screen_width;
    }

    list->scroll_offset_trg = (float)scroll;
}
