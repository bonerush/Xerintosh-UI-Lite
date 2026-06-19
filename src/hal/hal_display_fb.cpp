/**
 * @file   hal_display_fb.cpp
 * @brief  HAL 显示层帧缓冲管理实现
 * @details 负责显示子系统生命周期：初始化、方向/亮度配置、清屏、刷新。
 *          双实现架构：NATIVE_TEST 使用内存帧缓冲，硬件环境使用 M5Canvas。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_display.h"
#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：软件帧缓冲 ═══ */

uint16_t g_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];  /* RGB565 帧缓冲区 */
extern uint16_t *g_font_fb;  /* 定义在 hal_display_font.cpp，native 字体层 */
static int g_rotation = 0;
static uint8_t g_brightness = 128;

/**
 * @brief 初始化显示（清空帧缓冲与字体层为黑色）
 */
void hal_display_init(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
    if (g_font_fb != NULL) {
        memset(g_font_fb, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    }
}

/**
 * @brief 反初始化显示（native 桩：空操作）
 */
void hal_display_deinit(void) {
}

void hal_display_set_rotation(int rotation) {
    if (rotation < 0 || rotation > 3) rotation = 0;
    g_rotation = rotation;
}

int hal_display_get_rotation(void) {
    return g_rotation;
}

void hal_display_set_brightness(uint8_t level) {
    g_brightness = level;
}

uint8_t hal_display_get_brightness(void) {
    return g_brightness;
}

/**
 * @brief 清屏（填充指定颜色）
 * @param color 16 位 RGB565 颜色
 */
void hal_display_clear_color(uint16_t color) {
    for (size_t i = 0; i < sizeof(g_framebuffer) / sizeof(g_framebuffer[0]); i++) {
        g_framebuffer[i] = color;
    }
}

/**
 * @brief 清屏（填充背景色）
 */
void hal_display_clear(void) {
    hal_display_clear_color(COLOR_BG);
}

/**
 * @brief 刷新到屏幕（native 环境无实际输出，空操作）
 */
void hal_display_flush(void) {
}

#else

/* ═══ 硬件环境：M5GFX 加速 ═══ */

#include <M5Unified.h>
#include <M5GFX.h>

M5Canvas* g_canvas = nullptr;       /* 离屏画布 */
static int g_rotation = 0;          /* 当前屏幕方向 */
static uint8_t g_brightness = 128;  /* 当前背光亮度 */

/**
 * @brief 统一封装 M5Canvas 创建顺序：必须先设置色深再创建精灵。
 * @note  该 helper 消除 setColorDepth/createSprite 顺序被人工颠倒的风险。
 */
static void hal_display_create_sprite(M5Canvas* canvas,
                                      int16_t w, int16_t h,
                                      uint8_t depth)
{
    canvas->setColorDepth(depth);
    canvas->createSprite(w, h);
}

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
    g_screen_width = M5.Display.width();
    g_screen_height = M5.Display.height();
    hal_display_create_sprite(g_canvas, g_screen_width, g_screen_height, 8);
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

void hal_display_set_rotation(int rotation) {
    if (rotation < 0 || rotation > 3) rotation = 0;
    g_rotation = rotation;
    M5.Display.setRotation(rotation);
    g_screen_width = M5.Display.width();
    g_screen_height = M5.Display.height();

    /* 重建 M5Canvas 精灵以匹配新的屏幕方向（P1-2）。
     * 调用方无需再手动调用 hal_display_init()。 */
    if (g_canvas) {
        g_canvas->deleteSprite();
        hal_display_create_sprite(g_canvas, g_screen_width, g_screen_height, 8);
    }
}

int hal_display_get_rotation(void) {
    return g_rotation;
}

void hal_display_set_brightness(uint8_t level) {
    g_brightness = level;
    M5.Display.setBrightness(level);
}

uint8_t hal_display_get_brightness(void) {
    return g_brightness;
}

/**
 * @brief 清屏（填充指定颜色）
 * @param color 16 位 RGB565 颜色
 */
void hal_display_clear_color(uint16_t color) {
    if (g_canvas) {
        g_canvas->fillScreen(color);
    }
}

/**
 * @brief 清屏（填充背景色）
 */
void hal_display_clear(void) {
    hal_display_clear_color(COLOR_BG);
}

/**
 * @brief 将画布内容推送到物理屏幕
 */
void hal_display_flush(void) {
    if (g_canvas) {
        g_canvas->pushSprite(&M5.Display, 0, 0);
    }
}

#endif
