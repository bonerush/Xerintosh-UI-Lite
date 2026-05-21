/**
 * @file   ui_draw_driver.h
 * @brief  OLED → HAL 宏桥接头文件
 * @details 将原始 OLED UI 框架的绘图函数名映射到 HAL 层接口，
 *          使 ui_drawer.c 等绘制代码无需修改即可适配 TFT 屏幕。
 *          同时定义全局绘图颜色变量。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_DRAW_DRIVER_H
#define UI_DRAW_DRIVER_H

#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 全局绘制颜色 ═══ */

extern uint16_t g_xerintosh_draw_color;  /* 当前前景色（1=白，0=黑） */

/* ═══ OLED → HAL 宏桥接层 ═══ */

#define get_ticks() hal_get_ticks()
#define delay(ms) hal_delay_ms(ms)
#define oled_set_font(font) hal_set_font(font)
#define oled_draw_str(x, y, str) hal_draw_string(x, y, str, g_xerintosh_draw_color)
#define oled_draw_UTF8(x, y, str) hal_draw_utf8(x, y, str, g_xerintosh_draw_color)
#define oled_get_str_width(str) hal_get_string_width(str)
#define oled_get_UTF8_width(str) hal_get_utf8_width(str)
#define oled_get_str_height() hal_get_font_height()
#define oled_draw_pixel(x, y) hal_draw_pixel(x, y, g_xerintosh_draw_color)
#define oled_draw_circle(x, y, r) hal_draw_circle(x, y, r, g_xerintosh_draw_color)
#define oled_draw_R_box(x, y, w, h, r) hal_draw_fill_round_rect(x, y, w, h, r, g_xerintosh_draw_color)
#define oled_draw_box(x, y, w, h) hal_draw_fill_rect(x, y, w, h, g_xerintosh_draw_color)
#define oled_draw_frame(x, y, w, h) hal_draw_rect(x, y, w, h, g_xerintosh_draw_color)
#define oled_draw_R_frame(x, y, w, h, r) hal_draw_round_rect(x, y, w, h, r, g_xerintosh_draw_color)
#define oled_draw_H_line(x, y, l) hal_draw_h_line(x, y, l, g_xerintosh_draw_color)
#define oled_draw_V_line(x, y, h) hal_draw_v_line(x, y, h, g_xerintosh_draw_color)
#define oled_draw_line(x1, y1, x2, y2) hal_draw_line(x1, y1, x2, y2, g_xerintosh_draw_color)
#define oled_draw_H_dotted_line(x, y, l) /* placeholder */
#define oled_draw_V_dotted_line(x, y, h) /* placeholder */
#define oled_draw_bMP(x, y, w, h, bmp) hal_draw_xbitmap(x, y, w, h, bmp)
#define oled_set_draw_color(color) g_xerintosh_draw_color = ((color) ? COLOR_FG : COLOR_BG)
#define oled_set_font_mode(mode) /* TFT doesn't need */
#define oled_set_font_direction(dir) /* TFT doesn't need */
#define oled_clear_buffer() hal_display_clear()
#define oled_send_buffer() hal_display_flush()
#define oled_send_area_buffer(x, y, w, h) /* not needed */

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 UI 驱动（依次初始化显示、系统时钟、按键输入）
 */
extern void xerintosh_ui_driver_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DRAW_DRIVER_H */
