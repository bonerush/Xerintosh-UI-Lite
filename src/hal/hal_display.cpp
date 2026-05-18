#include "hal_display.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef NATIVE_TEST

static uint16_t g_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

void hal_display_init(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

void hal_display_clear(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

void hal_display_flush(void) {
}

void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    g_framebuffer[y * SCREEN_WIDTH + x] = color;
}

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

void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (len < 0) {
        x += len + 1;
        len = -len;
    }
    for (int16_t i = 0; i < len; i++) {
        hal_draw_pixel(x + i, y, color);
    }
}

void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (len < 0) {
        y += len + 1;
        len = -len;
    }
    for (int16_t i = 0; i < len; i++) {
        hal_draw_pixel(x, y + i, color);
    }
}

void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    hal_draw_h_line(x, y, w, color);
    hal_draw_h_line(x, y + h - 1, w, color);
    hal_draw_v_line(x, y, h, color);
    hal_draw_v_line(x + w - 1, y, h, color);
}

void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            hal_draw_pixel(x + col, y + row, color);
        }
    }
}

void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0 || r <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int16_t x1 = x + r;
    int16_t x2 = x + w - 1 - r;
    int16_t y1 = y + r;
    int16_t y2 = y + h - 1 - r;

    hal_draw_h_line(x1, y, x2 - x1 + 1, color);
    hal_draw_h_line(x1, y + h - 1, x2 - x1 + 1, color);
    hal_draw_v_line(x, y1, y2 - y1 + 1, color);
    hal_draw_v_line(x + w - 1, y1, y2 - y1 + 1, color);

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

void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0 || r <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    hal_draw_fill_rect(x + r, y, w - 2 * r, h, color);
    hal_draw_fill_rect(x, y + r, w, h - 2 * r, color);

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

void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (r <= 0) return;
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t xi = 0;
    int16_t yi = r;

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

void hal_set_font(const void* font) {
    (void)font;
}

void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
}

void hal_draw_utf8(int16_t x, int16_t y, const char* str, uint16_t color) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
}

int16_t hal_get_string_width(const char* str) {
    (void)str;
    return 0;
}

int16_t hal_get_utf8_width(const char* str) {
    (void)str;
    return 0;
}

int16_t hal_get_font_height(void) {
    return 8;
}

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

#else

#include <M5Unified.h>
#include <M5GFX.h>

static M5Canvas* g_canvas = nullptr;

void hal_display_init(void) {
    if (!g_canvas) {
        g_canvas = new M5Canvas(&M5.Display);
    }
    g_canvas->setColorDepth(16);
    g_canvas->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
}

void hal_display_clear(void) {
    if (g_canvas) {
        g_canvas->fillScreen(COLOR_BG);
    }
}

void hal_display_flush(void) {
    if (g_canvas) {
        g_canvas->pushSprite(&M5.Display, 0, 0);
    }
}

void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (g_canvas) g_canvas->drawPixel(x, y, color);
}

void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (g_canvas) g_canvas->drawLine(x1, y1, x2, y2, color);
}

void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (g_canvas) g_canvas->drawFastHLine(x, y, len, color);
}

void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color) {
    if (g_canvas) g_canvas->drawFastVLine(x, y, len, color);
}

void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_canvas) g_canvas->drawRect(x, y, w, h, color);
}

void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_canvas) g_canvas->fillRect(x, y, w, h, color);
}

void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (g_canvas) g_canvas->drawRoundRect(x, y, w, h, r, color);
}

void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (g_canvas) g_canvas->fillRoundRect(x, y, w, h, r, color);
}

void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (g_canvas) g_canvas->drawCircle(x, y, r, color);
}

void hal_set_font(const void* font) {
    if (g_canvas) g_canvas->setFont((const lgfx::v1::IFont*)font);
}

void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!g_canvas || !str) return;
    g_canvas->setTextColor(color);
    g_canvas->setCursor(x, y);
    g_canvas->print(str);
}

void hal_draw_utf8(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!g_canvas || !str) return;
    g_canvas->setTextColor(color);
    g_canvas->setCursor(x, y);
    g_canvas->print(str);
}

int16_t hal_get_string_width(const char* str) {
    if (!g_canvas || !str) return 0;
    return g_canvas->textWidth(str);
}

int16_t hal_get_utf8_width(const char* str) {
    if (!g_canvas || !str) return 0;
    return g_canvas->textWidth(str);
}

int16_t hal_get_font_height(void) {
    if (!g_canvas) return 8;
    return g_canvas->fontHeight();
}

void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!g_canvas) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            int16_t py = y + row;
            uint16_t color = g_canvas->readPixel(px, py);
            g_canvas->drawPixel(px, py, color ^ 0xFFFF);
        }
    }
}

void hal_draw_xbitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap) {
    if (!g_canvas || !bitmap) return;
    g_canvas->drawXBitmap(x, y, bitmap, w, h, COLOR_FG);
}

#endif
