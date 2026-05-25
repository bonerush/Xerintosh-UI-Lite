/**
 * @file   main.cpp
 * @brief  M5Stick-C 固件主入口
 * @details 硬件环境主程序：初始化 M5Unified、NVS 存储、设置、显示驱动、
 *          UI 菜单及管理器，进入主循环处理输入、更新状态机及渲染 UI。
 *
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>

#include "app/storage.h"
#include "app/settings.h"
#include "app/app_init.h"

extern "C" {
int16_t g_anim_speed = 92;  /* 全局动画速度默认值 */
}

#ifndef NATIVE_TEST

extern "C" {
bool wifi_on = true;  /* WiFi 默认开关状态 */
bool bt_on = true;    /* 蓝牙默认开关状态 */
}

#include <M5Unified.h>
#include <M5GFX.h>

#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui/ui_draw_driver.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "app/serial_input.h"
#include "app/serial_monitor.h"
#include "app/wifi_manager.h"
#include "app/bt_manager.h"
#include "app/boot_screen.h"

/* ═══ 设置变更回调（由 app_init.c 引用）═══ */

static int16_t brightness = 50;  /* 当前硬件亮度缓存 */

/**
 * @brief 亮度变更回调
 * @note  将亮度等级转换为硬件 PWM 值并应用到屏幕
 */
extern "C" void on_brightness_change_cb(void)
{
    brightness = g_brightness_level * 10;
    uint8_t hw = (uint8_t)settings_brightness_hw_value();
    M5.Display.setBrightness(hw);
    storage_set_brightness(brightness);
}

/**
 * @brief 动画速度变更回调
 */
extern "C" void on_anim_speed_change_cb(void)
{
    g_anim_speed = settings_anim_speed_value();
    storage_set_anim_speed((uint8_t)g_anim_speed);
}

/**
 * @brief 动画开关变更回调
 */
extern "C" void on_anim_enabled_change_cb(void)
{
    storage_set_anim_enabled(g_anim_enabled);
}

/**
 * @brief 波特率变更回调
 * @note  保存新波特率等级到 NVS，并重新初始化 Serial
 */
extern "C" void on_serial_baud_change_cb(void)
{
    storage_set_serial_baud_rate(g_serial_baud_rate);
    Serial.end();
    Serial.begin(settings_serial_baud_hw_value(g_serial_baud_rate));
}

/**
 * @brief 屏幕方向变更回调
 * @note  M5StickC 实测 rotation 效果：
 *        setRotation(0) → 正常竖屏 (portrait)
 *        setRotation(1) → 正常横屏 (landscape)
 *        setRotation(2) → 反向竖屏
 *        setRotation(3) → 反向横屏
 */
extern "C" void on_screen_rotation_change_cb(void)
{
    /* 同步 bool 开关到等级值 */
    g_screen_rotation_level = g_is_landscape ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT;

    int16_t gfx_rotation = g_is_landscape ? 1 : 0;
    M5.Display.setRotation(gfx_rotation);
    storage_set_screen_rotation((uint8_t)g_screen_rotation_level);
    hal_display_init();
}

/* ═══ 入口 ═══ */

/**
 * @brief Arduino setup()：系统初始化
 * @note  初始化顺序：M5 硬件 → 串口 → 存储 → 设置 → 显示 → UI → 管理器
 */
void setup()
{
    M5.begin();

    Serial.begin(settings_serial_baud_hw_value(g_serial_baud_rate));
    delay(200);
    Serial.println("\n\n=== M5Stick BOOT ===");

    storage_init();
    settings_load_from_storage();

    brightness = g_brightness_level * 10;
    M5.Display.setBrightness((uint8_t)settings_brightness_hw_value());
    g_anim_speed = settings_anim_speed_value();

    /* M5StickC 实测 rotation 效果：
     *   setRotation(0) → 正常竖屏 (portrait)
     *   setRotation(1) → 正常横屏 (landscape) */
    g_is_landscape = (g_screen_rotation_level == ORIENTATION_LANDSCAPE);
    int16_t gfx_rotation = g_is_landscape ? 1 : 0;
    M5.Display.setRotation(gfx_rotation);

    xerintosh_ui_driver_init();
    boot_screen_show();
    app_init_ui();
    app_init_managers();

    xerintosh_init_core();
    g_in_xerintosh = true;
}

/**
 * @brief Arduino loop()：主循环
 * @note  每帧执行：输入处理 → 管理器更新 → 清屏 → UI 渲染 → 长按提示 → 刷新
 */
void loop()
{
    M5.update();
    serial_monitor_update();  /* DEBUG 模式下后台持续读取串口 */
    app_input_process();
    wifi_mgr_update();
    bt_mgr_update();
    hal_display_clear();
    xerintosh_ui_main_core();
    xerintosh_ui_widget_core();

    uint32_t dur_a = hal_input_get_press_duration(HAL_BTN_A);
    uint32_t dur_b = hal_input_get_press_duration(HAL_BTN_B);
    if (dur_a > 0 && dur_a < 500) {
        xerintosh_draw_long_press_hint(dur_a, 500);
    } else if (dur_b > 0 && dur_b < 500) {
        xerintosh_draw_long_press_hint(dur_b, 500);
    }

    hal_display_flush();
}

#endif /* NATIVE_TEST */
