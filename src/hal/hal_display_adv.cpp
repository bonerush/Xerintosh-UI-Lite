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
    if (x < 0 || x >= HAL_SCREEN_WIDTH || y < 0 || y >= HAL_SCREEN_HEIGHT) return NULL;
    return &g_framebuffer[y * HAL_SCREEN_WIDTH + x];
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
            if (px >= 0 && px < HAL_SCREEN_WIDTH && py >= 0 && py < HAL_SCREEN_HEIGHT) {
                g_framebuffer[py * HAL_SCREEN_WIDTH + px] ^= 0xFFFF;
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
            int16_t bitIndex = 7 - (col % 8);  /* XBM 标准：MSB 在前（P1-6） */
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
    if (x < 0 || x >= HAL_SCREEN_WIDTH || y < 0 || y >= HAL_SCREEN_HEIGHT) return 0;
    uint16_t v = g_framebuffer[y * HAL_SCREEN_WIDTH + x];
    if (g_font_fb != NULL) {
        uint16_t fv = g_font_fb[y * HAL_SCREEN_WIDTH + x];
        if (fv != 0) v = fv;
    }
    return v;
}


#else

/* ═══ 硬件环境：LovyanGFX 加速 ═══ */

#include <LovyanGFX.hpp>

#include "fonts/cn_font_subset.h"

/* 子集中文字体对象（U8G2 格式，仅包含源码使用的 844 个汉字） */
const lgfx::U8g2font cn_font = { lgfx_cn_font_subset };

extern lgfx::LGFX_Sprite* g_canvas;

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

    /* 静态缓冲：一次读取整个选择器区域，XOR 后一次写回，避免逐行 readRect/pushImage
     * 最大尺寸：横屏下选择器宽度 ≤160，高度 ≤22（字体+边距），160×22 = 3520 像素 = 7040 字节
     * 竖屏下只需 80×22 = 1760 像素。比之前的 160×30(9600B) 节省最多 2560B */
    #define XOR_BUF_MAX_PX 3520
    static uint16_t xor_buf[XOR_BUF_MAX_PX];
    int total = w * h;
    if (total > XOR_BUF_MAX_PX) return;

    g_canvas->readRect(x, y, w, h, xor_buf);
    for (int i = 0; i < total; i++)
        xor_buf[i] ^= 0xFFFF;
    g_canvas->pushImage(x, y, w, h, xor_buf);
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
