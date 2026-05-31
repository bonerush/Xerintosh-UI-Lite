/**
 * @file   shutdown_screen.c
 * @brief  关机画面实现
 * @details 复用 Xerintosh logo 绘制关机画面，底部显示 "GOOD BYE!"。
 *          硬件环境下延时后进入 ESP32 深度睡眠，native 环境为空操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "shutdown_screen.h"
#include "app/boot/boot_screen.h"
#include "hal/hal_display.h"
#include "hal/hal_layout.h"
#include "hal/hal_system.h"
#include <stddef.h>

#ifndef NATIVE_TEST
#include <esp_sleep.h>
#endif

/**
 * @brief 显示关机界面并延时 2 秒
 */
void shutdown_screen_show(void)
{
    int16_t body_w = 44;
    int16_t body_h = 60;
    int16_t body_x = HAL_CENTER_X(body_w);
    int16_t body_y = HAL_CENTER_Y(body_h) - HAL_MARGIN_SM * 2;

    hal_display_clear();

    boot_screen_draw_logo(body_x, body_y, 100);

    /* "GOOD BYE!" 文字 */
    const char* label = "GOOD BYE!";
    hal_set_font(NULL);
    int16_t tw = hal_get_string_width(label);
    int16_t tx = HAL_CENTER_X(tw);
    int16_t ty = body_y + body_h + HAL_MARGIN_LG;
    hal_draw_string(tx, ty, label, COLOR_FG);

    hal_display_flush();
    hal_delay_ms(2000);
}

/**
 * @brief 进入深度睡眠模式
 */
void shutdown_screen_power_off(void)
{
#ifndef NATIVE_TEST
    esp_deep_sleep_start();
#endif
}
