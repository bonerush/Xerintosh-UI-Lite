/**
 * @file   boot_screen.c
 * @brief  开机画面实现
 * @details 绘制 "Xerintosh" 品牌开机画面：白色圆角矩形机身、黑色屏幕、
 *          软盘槽及品牌文字。自动适配横竖屏，以短边为基准保持机身比例。
 *
 * @copyright Copyright (c) 2026
 */

#include "boot_screen.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 显示开机画面并延时 2 秒
 * @note  绘制元素：
 *        1. 白色圆角矩形机身（以短边为基准，保证横竖屏比例一致）
 *        2. 黑色填充屏幕区域（带白色边框）
 *        3. 白色软盘槽
 *        4. "Xerintosh" 品牌文字（居中）
 */
void boot_screen_show(void)
{
    int16_t sw = SCREEN_WIDTH;
    int16_t sh = SCREEN_HEIGHT;

    /* 以短边为基准，保证横竖屏下机身比例一致 */
    int16_t base = (sw < sh) ? sw : sh;
    int16_t body_w = base * 55 / 100;
    int16_t body_h = base * 75 / 100;
    int16_t body_x = (sw - body_w) / 2;
    int16_t body_y = (sh - body_h) / 2 - 4;

    /* 屏幕边框 */
    int16_t bezel = 3;
    int16_t screen_w = body_w - bezel * 2;
    int16_t screen_h = body_h * 45 / 100;
    int16_t screen_x = body_x + bezel;
    int16_t screen_y = body_y + bezel;

    /* 软盘槽 */
    int16_t slot_w = body_w * 5 / 10;
    int16_t slot_h = (base < 100) ? 2 : 3;
    int16_t slot_x = body_x + (body_w - slot_w) / 2;
    int16_t slot_y = body_y + body_h - bezel - slot_h - 2;

    /* 清屏 */
    hal_display_clear();

    /* 绘制机身（白色圆角矩形轮廓） */
    hal_draw_round_rect(body_x, body_y, body_w, body_h, 3, COLOR_FG);

    /* 绘制屏幕区域（黑色填充） */
    hal_draw_fill_rect(screen_x, screen_y, screen_w, screen_h, COLOR_BG);

    /* 绘制屏幕边框（白色） */
    hal_draw_rect(screen_x, screen_y, screen_w, screen_h, COLOR_FG);

    /* 绘制软盘槽（白色填充） */
    hal_draw_fill_rect(slot_x, slot_y, slot_w, slot_h, COLOR_FG);

    /* 绘制 "Xerintosh" 文字 */
    const char* label = "Xerintosh";
    hal_set_font(NULL);
    int16_t tw = hal_get_string_width(label);
    int16_t tx = (sw - tw) / 2;
    int16_t ty = body_y + body_h + 8;
    hal_draw_string(tx, ty, label, COLOR_FG);

    hal_display_flush();

    /* 延迟 2 秒 */
    hal_delay_ms(2000);
}
