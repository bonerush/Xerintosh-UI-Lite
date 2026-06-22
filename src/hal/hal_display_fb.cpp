/**
 * @file   hal_display_fb.cpp
 * @brief  HAL 显示层帧缓冲管理实现
 * @details 负责显示子系统生命周期：初始化、方向/亮度配置、清屏、刷新。
 *          双实现架构：NATIVE_TEST 使用内存帧缓冲，硬件环境使用 LovyanGFX。
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

/* ═══ 硬件环境：LovyanGFX 加速 ═══ */

#include <LovyanGFX.hpp>
#include "driver/gpio.h"
#include "hal_system.h"
#include "hal_axp192.h"

/* ── M5StickC 显示配置（ST7735S） ── */

class LGFX_M5StickC : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX_M5StickC(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk = GPIO_NUM_13;
            cfg.pin_mosi = GPIO_NUM_15;
            cfg.pin_miso = GPIO_NUM_14;
            cfg.pin_dc   = GPIO_NUM_23;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = GPIO_NUM_5;
            cfg.pin_rst  = GPIO_NUM_18;
            cfg.panel_width  = 80;
            cfg.panel_height = 160;
            cfg.offset_x = 26;
            cfg.offset_y = 1;
            cfg.offset_rotation = 0;
            cfg.readable = false;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_32;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
        }
        setPanel(&_panel_instance);
        setLight(&_light_instance);
    }
};

static LGFX_M5StickC g_lgfx;      /* LovyanGFX 显示实例 */
static lgfx::LGFX_Sprite* g_canvas = nullptr;  /* 离屏画布 */
static int g_rotation = 0;          /* 当前屏幕方向 */
static uint8_t g_brightness = 128;  /* 当前背光亮度 */

/**
 * @brief 统一封装 LovyanGFX Sprite 创建顺序：必须先设置色深再创建精灵。
 * @note  该 helper 消除 setColorDepth/createSprite 顺序被人工颠倒的风险。
 */
static void hal_display_create_sprite(lgfx::LGFX_Sprite* canvas,
                                      int16_t w, int16_t h,
                                      uint8_t depth)
{
    canvas->setColorDepth(depth);
    canvas->createSprite(w, h);
}

/**
 * @brief 初始化显示：创建 LovyanGFX Sprite 并设置颜色深度为 8bit（RGB332, 256 色）
 * @note  8-bit 帧缓冲 80×160×1 = 12.8KB，相比 16-bit 节省 12.8KB，
 *        相比 4-bit 多 256 色精度，确保 RED/GREEN 文本正确渲染。
 *        使 ESP32-PICO 无 PSRAM 也能同时运行 Classic BT SPP + UI 渲染。
 */
void hal_display_init(void) {
    if (!g_canvas) {
        g_canvas = new lgfx::LGFX_Sprite(&g_lgfx);
    }
    g_screen_width = g_lgfx.width();
    g_screen_height = g_lgfx.height();
    hal_display_create_sprite(g_canvas, g_screen_width, g_screen_height, 8);
}

/**
 * @brief 释放离屏帧缓冲（8-bit: 12.8KB），并删除 canvas 对象。
 * @note  调用后所有绘制 API 因 g_canvas 为空而跳过，避免 sprite 已释放后
 *        仍调用 LovyanGFX 绘图方法导致堆损坏。屏幕保持最后一次 pushSprite 的
 *        内容。再次调用 hal_display_init() 会重新创建 canvas 与 sprite。
 */
void hal_display_deinit(void) {
    if (g_canvas) {
        delete g_canvas;           /* 释放 canvas 对象及其关联资源 */
        g_canvas = nullptr;        /* 后续绘制 API 统一判空跳过 */
    }
}

void hal_display_set_rotation(int rotation) {
    if (rotation < 0 || rotation > 3) rotation = 0;
    g_rotation = rotation;
    g_lgfx.setRotation(rotation);
    g_screen_width = g_lgfx.width();
    g_screen_height = g_lgfx.height();

    /* 重建 LovyanGFX Sprite 以匹配新的屏幕方向（P1-2）。
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
    g_lgfx.setBrightness(level);
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
        g_canvas->pushSprite(&g_lgfx, 0, 0);
    }
}

#endif
