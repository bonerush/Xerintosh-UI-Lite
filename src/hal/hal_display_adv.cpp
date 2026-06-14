/**
 * @file   hal_display_adv.cpp
 * @brief  HAL 显示层高级绘制实现
 * @details XOR 反色矩形、XBM 位图、裁剪矩形、测试钩子。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_display.h"
#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST

extern uint16_t *g_font_fb;
extern int16_t  g_font_fb_w;
extern int16_t  g_font_fb_h;
extern uint16_t g_framebuffer[];

static uint16_t* fb_pixel_ptr(int16_t x, int16_t y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return NULL;
    return &g_framebuffer[y * SCREEN_WIDTH + x];
}

/* ─── 高级绘制（native 实现）─── */

/**
 * @brief XOR 反色矩形（native 实现：逐像素异或 0xFFFF）
 */
void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            int16_t py = y + row;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                g_framebuffer[py * SCREEN_WIDTH + px] ^= 0xFFFF;
            }
        }
    }
}

/**
 * @brief 绘制 XBM 位图
 */
void hal_draw_xbitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap) {
    if (!bitmap || w <= 0 || h <= 0) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            int16_t byteIndex = (col + row * w) / 8;
            int16_t bitIndex = col % 8;
            if (bitmap[byteIndex] & (1 << bitIndex)) {
                hal_draw_pixel(x + col, y + row, COLOR_FG);
            }
        }
    }
}

/**
 * @brief 设置裁剪矩形（native 桩：空操作）
 */
void hal_set_clip_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    (void)x; (void)y; (void)w; (void)h;
}

/**
 * @brief 清除裁剪矩形（native 桩：空操作）
 */
void hal_clear_clip_rect(void) {
}


/**
 * @brief 读取帧缓冲中指定坐标的像素值（测试验证用）
 */
uint16_t hal_test_fb_read(int16_t x, int16_t y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return 0;
    uint16_t v = g_framebuffer[y * SCREEN_WIDTH + x];
    if (g_font_fb != NULL) {
        uint16_t fv = g_font_fb[y * SCREEN_WIDTH + x];
        if (fv != 0) v = fv;
    }
    return v;
}


#else

/* ═══ 硬件环境：M5GFX 加速 ═══ */

#include <M5Unified.h>
#include <M5GFX.h>

#include "fonts/cn_font_subset.h"

/* 子集中文字体对象（U8G2 格式，仅包含源码使用的 844 个汉字） */
const lgfx::U8g2font cn_font = { lgfx_cn_font_subset };

extern M5Canvas* g_canvas;

void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!g_canvas || w <= 0 || h <= 0) return;

    int16_t cw = g_canvas->width();
    int16_t ch = g_canvas->height();
    /* 裁剪 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > cw) w = cw - x;
    if (y + h > ch) h = ch - y;
    if (w <= 0 || h <= 0) return;

    /* 行缓冲：最大 160×2 = 320 字节，栈安全 */
    uint16_t row_buf[160];

    for (int16_t row = 0; row < h; row++) {
        g_canvas->readRect(x, y + row, w, 1, row_buf);
        for (int16_t i = 0; i < w; i++)
            row_buf[i] ^= 0xFFFF;
        g_canvas->pushImage(x, y + row, w, 1, row_buf);
    }
}

/**
 * @brief 绘制 XBM 位图
 */
void hal_draw_xbitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap) {
    if (!g_canvas || !bitmap) return;
    g_canvas->drawXBitmap(x, y, bitmap, w, h, COLOR_FG);
}

/**
 * @brief 设置裁剪矩形
 */
void hal_set_clip_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (g_canvas) g_canvas->setClipRect(x, y, w, h);
}

/**
 * @brief 清除裁剪矩形
 */
void hal_clear_clip_rect(void) {
    if (g_canvas) g_canvas->clearClipRect();
}

/**
 * @brief 获取中文字体指针
 * @return 子集中文字体（U8G2 格式，仅包含源码使用的汉字）
 */
const void* hal_get_cn_font(void) {
    return &cn_font;
}


#endif
