/**
 * @file   hal_display_draw.cpp
 * @brief  HAL 显示层绘制原语实现
 * @details 像素、线段、矩形、圆、XOR 反色、XBM 位图、裁剪矩形。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_display.h"
#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST

static uint16_t* fb(void) { return hal_display_framebuffer(); }

/**
 * @brief 绘制像素点（带边界检查）
 */
void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= HAL_SCREEN_WIDTH || y < 0 || y >= HAL_SCREEN_HEIGHT) return;
    fb()[y * HAL_SCREEN_WIDTH + x] = color;
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

#else

/* ═══ 硬件环境：LovyanGFX 加速 ═══ */

#include <LovyanGFX.hpp>

static lgfx::LGFX_Sprite* canvas(void) {
    return reinterpret_cast<lgfx::LGFX_Sprite*>(hal_display_canvas());
}

void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (canvas()) canvas()->drawPixel(x, y, color);
}

/**
 * @brief 绘制线段
 */
void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (canvas()) canvas()->drawLine(x1, y1, x2, y2, color);
}

/**
 * @brief 绘制水平线段
 */
void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (canvas()) canvas()->drawFastHLine(x, y, len, color);
}

/**
 * @brief 绘制垂直线段
 */
void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (canvas()) canvas()->drawFastVLine(x, y, len, color);
}

/**
 * @brief 绘制空心矩形
 */
void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (canvas()) canvas()->drawRect(x, y, w, h, color);
}

/**
 * @brief 绘制实心矩形
 */
void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (canvas()) canvas()->fillRect(x, y, w, h, color);
}

/**
 * @brief 绘制空心圆角矩形
 */
void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (canvas()) canvas()->drawRoundRect(x, y, w, h, r, color);
}

/**
 * @brief 绘制实心圆角矩形
 */
void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (canvas()) canvas()->fillRoundRect(x, y, w, h, r, color);
}

/**
 * @brief 绘制空心圆
 */
void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (canvas()) canvas()->drawCircle(x, y, r, color);
}

/**
 * @brief 设置当前字体
 */

#endif
