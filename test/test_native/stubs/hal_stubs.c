/**
 * @file   hal_stubs.c
 * @brief  Native 测试环境的 HAL 桩实现
 * @details 为物理设备（dev_fb0, dev_input0）的 native 测试提供
 *          可检查的 fake 实现。所有操作记录在全局 fake 结构中供测试断言。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ═══ Fake 显示状态 ═══ */

static int g_fake_pixel_count = 0;
static int g_fake_clear_count = 0;
static int g_fake_flush_count = 0;
static int16_t g_fake_last_px = 0;
static int16_t g_fake_last_py = 0;
static uint16_t g_fake_last_pcolor = 0;
static uint16_t g_fake_last_clear_color = 0;
static int g_fake_char_count = 0;
static int16_t g_fake_last_cx = 0;
static int16_t g_fake_last_cy = 0;
static char g_fake_last_char = 0;
static uint16_t g_fake_last_cfg = 0;
static uint16_t g_fake_last_cbg = 0;

void fake_display_reset(void)
{
    g_fake_pixel_count = 0;
    g_fake_clear_count = 0;
    g_fake_flush_count = 0;
    g_fake_last_px = 0;
    g_fake_last_py = 0;
    g_fake_last_pcolor = 0;
    g_fake_last_clear_color = 0;
    g_fake_char_count = 0;
    g_fake_last_cx = 0;
    g_fake_last_cy = 0;
    g_fake_last_char = 0;
    g_fake_last_cfg = 0;
    g_fake_last_cbg = 0;
}

int fake_display_get_pixel_count(void) { return g_fake_pixel_count; }
int fake_display_get_clear_count(void) { return g_fake_clear_count; }
int fake_display_get_flush_count(void) { return g_fake_flush_count; }

void fake_display_get_last_pixel(int16_t *x, int16_t *y, uint16_t *color)
{
    if (x) *x = g_fake_last_px;
    if (y) *y = g_fake_last_py;
    if (color) *color = g_fake_last_pcolor;
}

uint16_t fake_display_get_last_clear_color(void) { return g_fake_last_clear_color; }
int fake_display_get_char_count(void) { return g_fake_char_count; }

void fake_display_get_last_char(int16_t *x, int16_t *y, char *c, uint16_t *fg, uint16_t *bg)
{
    if (x) *x = g_fake_last_cx;
    if (y) *y = g_fake_last_cy;
    if (c) *c = g_fake_last_char;
    if (fg) *fg = g_fake_last_cfg;
    if (bg) *bg = g_fake_last_cbg;
}

/* ═══ Fake 输入状态 ═══ */

static bool g_fake_btn_a = false;
static bool g_fake_btn_b = false;

void fake_input_reset(void)
{
    g_fake_btn_a = false;
    g_fake_btn_b = false;
}

void fake_input_set_button(char name, bool pressed)
{
    if (name == 'A') g_fake_btn_a = pressed;
    if (name == 'B') g_fake_btn_b = pressed;
}

/* ═══ Fake 系统状态 ═══ */

static uint32_t g_fake_ticks = 0;

void fake_system_set_ticks(uint32_t ticks) { g_fake_ticks = ticks; }
uint32_t fake_system_get_ticks(void) { return g_fake_ticks; }

#endif /* NATIVE_TEST */
