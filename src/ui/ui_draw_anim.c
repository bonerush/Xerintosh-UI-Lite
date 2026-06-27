/**
 * @file   ui_draw_anim.c
 * @brief  UI 退场动画实现
 * @details 绘制退场遮罩动画（沙漏 + 扫描线效果）。
 *          主入口 xerintosh_draw_exit_animation() 委托给三个子阶段函数。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_drawer.h"
#include "ui_core.h"

/* ═══ 退场动画 ═══ */

/**
 * @brief 绘制沙漏图案（含颗粒点）
 * @param _x_hourglass_offset 沙漏左上角 x
 * @param _y_hourglass        沙漏左上角 y
 */
static void draw_hourglass(int16_t _x_hourglass_offset, int16_t _y_hourglass)
{
    g_xerintosh_draw_color = COLOR_FG;

    /* 上半部分 */
    hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 2, EXIT_ANIM_HOURGLASS_W, 3, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 3, 9, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_FG;

    hal_draw_v_line(_x_hourglass_offset + 1, _y_hourglass + 4, 5, g_xerintosh_draw_color);
    hal_draw_v_line(_x_hourglass_offset + 11, _y_hourglass + 4, 5, g_xerintosh_draw_color);

    /* 中间填充 */
    for (uint8_t i = 0; i < 5; ++i) {
        int8_t _current_y = _y_hourglass + 8 + i;
        int8_t _left_x = (i < 3) ? (_x_hourglass_offset + 1 + i) : (_x_hourglass_offset + 4);
        int8_t _right_x = (i < 3) ? (_x_hourglass_offset + 10 - i) : (_x_hourglass_offset + 7);
        hal_draw_h_line(_left_x, _current_y, 2, g_xerintosh_draw_color);
        hal_draw_h_line(_right_x, _current_y, 2, g_xerintosh_draw_color);
    }

    for (uint8_t i = 0; i < 3; ++i) {
        int8_t _current_y = _y_hourglass + 13 + i;
        hal_draw_h_line(_x_hourglass_offset + 3 - i, _current_y, 2, g_xerintosh_draw_color);
        hal_draw_h_line(_x_hourglass_offset + 8 + i, _current_y, 2, g_xerintosh_draw_color);
    }

    hal_draw_v_line(_x_hourglass_offset + 1, _y_hourglass + 16, 3, g_xerintosh_draw_color);
    hal_draw_v_line(_x_hourglass_offset + 11, _y_hourglass + 16, 3, g_xerintosh_draw_color);

    /* 下半部分 */
    hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 19, EXIT_ANIM_HOURGLASS_W, 3, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 20, 9, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_FG;

    /* 颗粒点 */
    const uint8_t _points[][2] = {
        {5, 7}, {7, 7}, {6, 8}, {6, 10}, {6, 14}, {6, 16},
        {5, 17}, {7, 17}, {4, 18}, {6, 18}, {8, 18}
    };
    for (uint8_t i = 0; i < sizeof(_points) / sizeof(_points[0]); ++i)
        hal_draw_pixel(_x_hourglass_offset + _points[i][0], _y_hourglass + _points[i][1], g_xerintosh_draw_color);
}

/**
 * @brief 绘制底部扫描线和交错像素扫描效果
 */
static void draw_scanlines(void)
{
    g_xerintosh_draw_color = COLOR_FG;

    /* 底部扫描线 */
    if (g_xerintosh_exit_anim_temp_h + EXIT_ANIM_SCANLINE_OVERDRAW >= 0)
        for (uint8_t i = 0; i <= EXIT_ANIM_SCANLINE_OVERDRAW; ++i)
            hal_draw_h_line(0, g_xerintosh_exit_anim_temp_h + i, HAL_SCREEN_WIDTH, g_xerintosh_draw_color);

    /* 交错像素扫描效果 */
    for (int16_t i = 0; i <= HAL_SCREEN_WIDTH; i += 2)
        for (int16_t j = g_xerintosh_exit_anim_temp_h - EXIT_ANIM_SCANLINE_TRAIL_PX; j <= g_xerintosh_exit_anim_temp_h - 1; j++) {
            if (j % 2 == 0)
                hal_draw_pixel(i + 1, j, g_xerintosh_draw_color);
            if (j % 2 == 1)
                hal_draw_pixel(i, j, g_xerintosh_draw_color);
        }
}

/**
 * @brief 退场动画状态机（阶段切换与完成判定）
 */
static void update_exit_anim_state(void)
{
    /* 状态机转换：使用范围判断代替浮点精确相等，避免多次循环后卡死 */
    if (g_xerintosh_exit_animation_status == 0 && g_xerintosh_exit_anim_temp_h >= HAL_SCREEN_HEIGHT + EXIT_ANIM_OVERDRAW_PX - EXIT_ANIM_SNAP_PX) {
        g_xerintosh_exit_anim_temp_h = HAL_SCREEN_HEIGHT + EXIT_ANIM_OVERDRAW_PX;
        g_xerintosh_exit_animation_status = 1;
        return;
    }

    if (g_xerintosh_exit_animation_status == 1) {
        g_xerintosh_exit_anim_temp_h_trg = -EXIT_ANIM_OVERDRAW_PX;
        g_xerintosh_exit_animation_status = 2;
        return;
    }

    if (g_xerintosh_exit_animation_status == 2 && g_xerintosh_exit_anim_temp_h <= -EXIT_ANIM_OVERDRAW_PX + EXIT_ANIM_SNAP_PX) {
        g_xerintosh_exit_animation_finished = true;
        g_xerintosh_exit_animation_status = 0;
        g_xerintosh_exit_anim_temp_h = -EXIT_ANIM_OVERDRAW_PX;
        g_xerintosh_exit_anim_temp_h_trg = HAL_SCREEN_HEIGHT + EXIT_ANIM_OVERDRAW_PX;
        return;
    }
}

/**
 * @brief 绘制退场遮罩动画（沙漏 + 扫描线效果）
 * @note  使用 ui_context 中的退出动画状态控制遮罩高度，经历三个阶段：
 *        阶段 0：从 -8 向下展开到 HAL_SCREEN_HEIGHT+8
 *        阶段 1：到达底部后触发状态切换
 *        阶段 2：回缩到 -8 并标记动画完成
 */
void xerintosh_draw_exit_animation()
{
    /* 每次新动画开始时强制重置状态，避免残留导致卡死 */
    if (g_xerintosh_exit_anim_last_finished && !g_xerintosh_exit_animation_finished) {
        g_xerintosh_exit_anim_temp_h = -EXIT_ANIM_OVERDRAW_PX;
        g_xerintosh_exit_anim_temp_h_trg = HAL_SCREEN_HEIGHT + EXIT_ANIM_OVERDRAW_PX;
        g_xerintosh_exit_animation_status = 0;
    }
    g_xerintosh_exit_anim_last_finished = g_xerintosh_exit_animation_finished;

    /* 屏幕方向/尺寸切换时，防止目标值残留旧尺寸 */
    if (g_xerintosh_exit_anim_prev_screen_h != HAL_SCREEN_HEIGHT) {
        float max_h = (float)HAL_SCREEN_HEIGHT + (float)EXIT_ANIM_OVERDRAW_PX;
        if (g_xerintosh_exit_anim_temp_h > max_h) g_xerintosh_exit_anim_temp_h = max_h;
        if (g_xerintosh_exit_animation_status == 0) g_xerintosh_exit_anim_temp_h_trg = max_h;
        g_xerintosh_exit_anim_prev_screen_h = (int16_t)HAL_SCREEN_HEIGHT;
    }

    /* 绘制全屏黑色遮罩 */
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_fill_rect(0, 0, HAL_SCREEN_WIDTH, g_xerintosh_exit_anim_temp_h, g_xerintosh_draw_color);

    /* 沙漏 */
    uint8_t _x_hourglass_offset = HAL_SCREEN_WIDTH / 2 - EXIT_ANIM_HOURGLASS_OFFSET_X;
    int8_t _y_hourglass = g_xerintosh_exit_anim_temp_h - HAL_SCREEN_HEIGHT / 2 - EXIT_ANIM_HOURGLASS_CENTER_OFFSET_Y;
    if (_y_hourglass + EXIT_ANIM_HOURGLASS_H - 2 >= 0)
        draw_hourglass(_x_hourglass_offset, _y_hourglass);

    /* 扫描线 */
    draw_scanlines();

    /* 动画插值 */
    xerintosh_animation(&g_xerintosh_exit_anim_temp_h, g_xerintosh_exit_anim_temp_h_trg, ANIM_SPEED_EXIT);

    /* 状态机 */
    update_exit_anim_state();
}
