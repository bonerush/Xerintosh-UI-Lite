/**
 * @file   hal_display.cpp
 * @brief  HAL 显示层实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：使用内存帧缓冲区 g_framebuffer，软件实现全部绘制算法
 *          - 硬件环境时：使用 M5Unified/M5GFX 的 M5Canvas 进行硬件加速绘制
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_display.h"
#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：软件帧缓冲 ═══ */

static uint16_t g_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];  /* RGB565 帧缓冲区 */

/**
 * @brief 初始化显示（清空帧缓冲为黑色）
 */
void hal_display_init(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

/**
 * @brief 清屏
 */
void hal_display_clear(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

/**
 * @brief 刷新到屏幕（native 环境无实际输出，空操作）
 */
void hal_display_flush(void) {
}

/**
 * @brief 绘制像素点（带边界检查）
 */
void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    g_framebuffer[y * SCREEN_WIDTH + x] = color;
}

/**
 * @brief 绘制线段（Bresenham 算法）
 */
void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    int16_t dx = abs(x2 - x1);
    int16_t dy = abs(y2 - y1);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        hal_draw_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/**
 * @brief 绘制水平线段（支持负长度）
 */
void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (len < 0) {
        x += len + 1;
        len = -len;
    }
    for (int16_t i = 0; i < len; i++) {
        hal_draw_pixel(x + i, y, color);
    }
}

/**
 * @brief 绘制垂直线段（支持负长度）
 */
void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (len < 0) {
        y += len + 1;
        len = -len;
    }
    for (int16_t i = 0; i < len; i++) {
        hal_draw_pixel(x, y + i, color);
    }
}

/**
 * @brief 绘制空心矩形
 */
void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    hal_draw_h_line(x, y, w, color);
    hal_draw_h_line(x, y + h - 1, w, color);
    hal_draw_v_line(x, y, h, color);
    hal_draw_v_line(x + w - 1, y, h, color);
}

/**
 * @brief 绘制实心矩形
 */
void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            hal_draw_pixel(x + col, y + row, color);
        }
    }
}

/**
 * @brief 绘制空心圆角矩形（中点圆算法画四角）
 */
void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0 || r <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int16_t x1 = x + r;
    int16_t x2 = x + w - 1 - r;
    int16_t y1 = y + r;
    int16_t y2 = y + h - 1 - r;

    /* 四条直边 */
    hal_draw_h_line(x1, y, x2 - x1 + 1, color);
    hal_draw_h_line(x1, y + h - 1, x2 - x1 + 1, color);
    hal_draw_v_line(x, y1, y2 - y1 + 1, color);
    hal_draw_v_line(x + w - 1, y1, y2 - y1 + 1, color);

    /* 四个圆角 */
    int16_t cx = x + r;
    int16_t cy = y + r;
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t xi = 0;
    int16_t yi = r;

    while (xi <= yi) {
        hal_draw_pixel(cx + xi - r, cy - yi, color);
        hal_draw_pixel(cx - xi, cy - yi, color);
        hal_draw_pixel(cx + xi - r, cy + yi - r, color);
        hal_draw_pixel(cx - xi, cy + yi - r, color);
        hal_draw_pixel(cx + yi - r, cy - xi, color);
        hal_draw_pixel(cx - yi, cy - xi, color);
        hal_draw_pixel(cx + yi - r, cy + xi - r, color);
        hal_draw_pixel(cx - yi, cy + xi - r, color);
        if (f >= 0) {
            yi--;
            ddF_y += 2;
            f += ddF_y;
        }
        xi++;
        ddF_x += 2;
        f += ddF_x;
    }
}

/**
 * @brief 绘制实心圆角矩形
 */
void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0 || r <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    hal_draw_fill_rect(x + r, y, w - 2 * r, h, color);
    hal_draw_fill_rect(x, y + r, w, h - 2 * r, color);

    /* 填充四个圆角 */
    int16_t cx = x + r;
    int16_t cy = y + r;
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t xi = 0;
    int16_t yi = r;

    while (xi <= yi) {
        hal_draw_h_line(cx - yi, cy - xi, 2 * yi, color);
        hal_draw_h_line(cx - yi, cy + xi - r, 2 * yi, color);
        hal_draw_h_line(cx - xi, cy - yi, 2 * xi, color);
        hal_draw_h_line(cx - xi, cy + yi - r, 2 * xi, color);
        if (f >= 0) {
            yi--;
            ddF_y += 2;
            f += ddF_y;
        }
        xi++;
        ddF_x += 2;
        f += ddF_x;
    }
}

/**
 * @brief 绘制空心圆（中点圆算法）
 */
void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (r <= 0) return;
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t xi = 0;
    int16_t yi = r;

    /* 四个轴对称点 */
    hal_draw_pixel(x, y + r, color);
    hal_draw_pixel(x, y - r, color);
    hal_draw_pixel(x + r, y, color);
    hal_draw_pixel(x - r, y, color);

    while (xi < yi) {
        if (f >= 0) {
            yi--;
            ddF_y += 2;
            f += ddF_y;
        }
        xi++;
        ddF_x += 2;
        f += ddF_x;
        hal_draw_pixel(x + xi, y + yi, color);
        hal_draw_pixel(x - xi, y + yi, color);
        hal_draw_pixel(x + xi, y - yi, color);
        hal_draw_pixel(x - xi, y - yi, color);
        hal_draw_pixel(x + yi, y + xi, color);
        hal_draw_pixel(x - yi, y + xi, color);
        hal_draw_pixel(x + yi, y - xi, color);
        hal_draw_pixel(x - yi, y - xi, color);
    }
}

/* ─── 字体与文本（native 桩函数）─── */

/**
 * @brief 设置当前字体（native 桩：空操作）
 */
void hal_set_font(const void* font) {
    (void)font;
}

/**
 * @brief 绘制 ASCII 字符串（native 桩：空操作）
 */
void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
}

/**
 * @brief 获取 ASCII 字符串宽度（native 桩：始终返回 0）
 */
int16_t hal_get_string_width(const char* str) {
    (void)str;
    return 0;
}

/**
 * @brief 获取当前字体高度（native 桩：返回固定值 8）
 */
int16_t hal_get_font_height(void) {
    return 8;
}

/**
 * @brief 获取中文字体指针（native 桩：返回 NULL）
 */
const void* hal_get_cn_font(void) {
    return NULL;
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

/* ─── 测试钩子 ─── */

/**
 * @brief 读取帧缓冲中指定坐标的像素值（测试验证用）
 */
uint16_t hal_test_fb_read(int16_t x, int16_t y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return 0;
    return g_framebuffer[y * SCREEN_WIDTH + x];
}

#else

/* ═══ 硬件环境：M5GFX 加速 ═══ */

#include <M5Unified.h>
#include <M5GFX.h>

#include "fonts/cn_font_subset.h"

/* 子集中文字体对象（U8G2 格式，仅包含源码使用的 844 个汉字） */
const lgfx::U8g2font cn_font = { lgfx_cn_font_subset };

static M5Canvas* g_canvas = nullptr;  /* 离屏画布 */
int16_t g_screen_width = 160;         /* 默认屏幕宽度，init 时从硬件读取 */
int16_t g_screen_height = 80;         /* 默认屏幕高度 */

/**
 * @brief 初始化显示：创建 M5Canvas 并设置颜色深度为 8bit（RGB332, 256 色）
 * @note  8-bit 帧缓冲 80×160×1 = 12.8KB，相比 16-bit 节省 12.8KB，
 *        相比 4-bit 多 256 色精度，确保 RED/GREEN 文本正确渲染。
 *        使 ESP32-PICO 无 PSRAM 也能同时运行 Classic BT SPP + UI 渲染。
 */
void hal_display_init(void) {
    if (!g_canvas) {
        g_canvas = new M5Canvas(&M5.Display);
    }
    g_canvas->setColorDepth(8);   /* 8-bit 色深：RGB332, 256 色，每像素 1 字节 */
    g_screen_width = M5.Display.width();
    g_screen_height = M5.Display.height();
    g_canvas->createSprite(g_screen_width, g_screen_height);
}


/**
 * @brief 释放离屏帧缓冲（8-bit: 12.8KB），保留 canvas 对象以便后续重新 createSprite
 * @note  调用后所有绘制 API 因 sprite 不存在而跳过。屏幕保持最后一次
 *        pushSprite 的内容。再次调用 hal_display_init() 可恢复。
 */
void hal_display_deinit(void) {
    if (g_canvas) {
        g_canvas->deleteSprite();  /* 释放 80×160×8bit = 12.8KB 帧缓冲 */
    }
}

/**
 * @brief 清屏（填充背景色）
 */
void hal_display_clear(void) {
    if (g_canvas) {
        g_canvas->fillScreen(COLOR_BG);
    }
}

/**
 * @brief 将画布内容推送到物理屏幕
 */
void hal_display_flush(void) {
    if (g_canvas) {
        g_canvas->pushSprite(&M5.Display, 0, 0);
    }
}

/**
 * @brief 绘制像素点
 */
void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (g_canvas) g_canvas->drawPixel(x, y, color);
}

/**
 * @brief 绘制线段
 */
void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (g_canvas) g_canvas->drawLine(x1, y1, x2, y2, color);
}

/**
 * @brief 绘制水平线段
 */
void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (g_canvas) g_canvas->drawFastHLine(x, y, len, color);
}

/**
 * @brief 绘制垂直线段
 */
void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (g_canvas) g_canvas->drawFastVLine(x, y, len, color);
}

/**
 * @brief 绘制空心矩形
 */
void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_canvas) g_canvas->drawRect(x, y, w, h, color);
}

/**
 * @brief 绘制实心矩形
 */
void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_canvas) g_canvas->fillRect(x, y, w, h, color);
}

/**
 * @brief 绘制空心圆角矩形
 */
void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (g_canvas) g_canvas->drawRoundRect(x, y, w, h, r, color);
}

/**
 * @brief 绘制实心圆角矩形
 */
void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (g_canvas) g_canvas->fillRoundRect(x, y, w, h, r, color);
}

/**
 * @brief 绘制空心圆
 */
void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (g_canvas) g_canvas->drawCircle(x, y, r, color);
}

/**
 * @brief 设置当前字体
 */
void hal_set_font(const void* font) {
    if (!g_canvas) return;
    if (font)
        g_canvas->setFont((const lgfx::v1::IFont*)font);
    else
        g_canvas->setFont(&fonts::Font0);
}

/**
 * @brief 绘制 ASCII 字符串
 */
void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!g_canvas || !str) return;
    g_canvas->setTextColor(color);
    g_canvas->setTextDatum(lgfx::v1::baseline_left);
    g_canvas->drawString(str, x, y);
}

/**
 * @brief 获取 ASCII 字符串宽度
 */
int16_t hal_get_string_width(const char* str) {
    if (!g_canvas || !str) return 0;
    return g_canvas->textWidth(str);
}

/**
 * @brief 获取当前字体高度
 */
int16_t hal_get_font_height(void) {
    if (!g_canvas) return 8;
    return g_canvas->fontHeight();
}

/**
 * @brief XOR 反色矩形（硬件实现：逐行 readRect-异或-pushImage）
 * @note  使用 readRect/pushImage 而非裸帧缓冲指针，以兼容 4/8/16-bit
 *        色深。readRect 始终返回 RGB565，异或后 pushImage 自动转换回
 *        当前色深。帧缓冲在内存中（M5Canvas），操作无 SPI 开销。
 * @note  最大行宽 160px，临时行缓冲仅 320 字节，栈安全。
 */
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
