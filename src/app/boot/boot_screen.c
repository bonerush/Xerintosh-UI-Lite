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
 * @brief 在指定位置绘制 Xerintosh 设备 logo
 * @note  绘制元素：白色圆角矩形机身、黑色屏幕区域、软盘槽。
 *         不含 "Xerintosh" 文字（由调用者自行绘制）。
 */
void boot_screen_draw_logo(int16_t ox, int16_t oy, int16_t scale)
{
    /* 基准尺寸（scale=100 时的像素） */
    int16_t body_w = 44 * scale / 100;
    int16_t body_h = 60 * scale / 100;
    if (body_w < 20) body_w = 20;
    if (body_h < 20) body_h = 20;

    int16_t bezel    = (scale < 80) ? 2 : 3;
    int16_t screen_w = body_w - bezel * 2;
    int16_t screen_h = body_h * 45 / 100;
    int16_t screen_x = ox + bezel;
    int16_t screen_y = oy + bezel;

    int16_t slot_w = body_w * 5 / 10;
    int16_t slot_h = (scale < 80) ? 2 : 3;
    int16_t slot_x = ox + (body_w - slot_w) / 2;
    int16_t slot_y = oy + body_h - bezel - slot_h - 2;

    /* 机身（白色圆角矩形轮廓） */
    hal_draw_round_rect(ox, oy, body_w, body_h, 3, COLOR_FG);

    /* 屏幕区域（黑色填充） */
    hal_draw_fill_rect(screen_x, screen_y, screen_w, screen_h, COLOR_BG);

    /* 屏幕边框（白色） */
    hal_draw_rect(screen_x, screen_y, screen_w, screen_h, COLOR_FG);

    /* 软盘槽（白色填充） */
    hal_draw_fill_rect(slot_x, slot_y, slot_w, slot_h, COLOR_FG);
}

/**
 * @brief 显示开机画面并延时 2 秒
 */
void boot_screen_show(void)
{
    int16_t sw = SCREEN_WIDTH;
    int16_t sh = SCREEN_HEIGHT;

    int16_t body_w = 44;
    int16_t body_h = 60;
    int16_t body_x = (sw - body_w) / 2;
    int16_t body_y = (sh - body_h) / 2 - 4;

    hal_display_clear();

    boot_screen_draw_logo(body_x, body_y, 100);

    /* "Xerintosh" 文字 */
    const char* label = "Xerintosh";
    hal_set_font(NULL);
    int16_t tw = hal_get_string_width(label);
    int16_t tx = (sw - tw) / 2;
    int16_t ty = body_y + body_h + 8;
    hal_draw_string(tx, ty, label, COLOR_FG);

    hal_display_flush();
    hal_delay_ms(2000);
}
