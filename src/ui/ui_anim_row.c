/**
 * @file   ui_anim_row.c
 * @brief  公共行列表动画工具实现
 * @details 封装 xerintosh_animation() 循环，提供行列表动画的
 *          初始化、逐帧更新、目标刷新三个核心操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_anim_row.h"
#include "ui_core.h"
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
        list->rows[i].y = (float)SCREEN_HEIGHT;  /* 入场起点：屏幕底部 */
        list->rows[i].y_trg = (float)(list_top + i * row_height);
        list->rows[i].w = (float)SCREEN_WIDTH;
        list->rows[i].w_trg = (float)SCREEN_WIDTH;
    }

    list->highlight.y = (float)SCREEN_HEIGHT;
    list->highlight.y_trg = (float)list_top;
    list->highlight.w = (float)SCREEN_WIDTH;
    list->highlight.w_trg = (float)SCREEN_WIDTH;

    list->scroll_offset = 0.0f;
    list->scroll_offset_trg = 0.0f;
}

/* ═══ 逐帧更新 ═══ */

bool xerintosh_anim_row_list_update(xerintosh_anim_row_list_t *list, float speed)
{
    bool all_settled = true;

    for (int i = 0; i < list->visible_count && i < ANIM_ROW_MAX; i++) {
        xerintosh_animation(&list->rows[i].y, list->rows[i].y_trg, speed);
        xerintosh_animation(&list->rows[i].w, list->rows[i].w_trg, speed);
        if (fabsf(list->rows[i].y - list->rows[i].y_trg) > 0.5f)
            all_settled = false;
    }

    xerintosh_animation(&list->highlight.y, list->highlight.y_trg, speed);
    xerintosh_animation(&list->highlight.w, list->highlight.w_trg, speed);
    if (fabsf(list->highlight.y - list->highlight.y_trg) > 0.5f)
        all_settled = false;

    xerintosh_animation(&list->scroll_offset, list->scroll_offset_trg, speed);

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
