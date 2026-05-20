#include <stdint.h>

#include "app/storage.h"
#include "app/settings.h"
#include "app/app_init.h"

extern "C" {
int16_t g_anim_speed = 92;
}

#ifndef NATIVE_TEST

extern "C" {
bool wifi_on = true;
bool bt_on = true;
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
#include "app/wifi_manager.h"
#include "app/bt_manager.h"
#include "app/boot_screen.h"

/* ─── 设置变更回调（由 app_init.c 引用） ─── */

static int16_t brightness = 50;

extern "C" void on_brightness_change_cb(void)
{
    brightness = g_brightness_level * 10;
    uint8_t hw = (uint8_t)settings_brightness_hw_value();
    M5.Display.setBrightness(hw);
    storage_set_brightness(brightness);
}

extern "C" void on_anim_speed_change_cb(void)
{
    g_anim_speed = settings_anim_speed_value();
    storage_set_anim_speed((uint8_t)g_anim_speed);
}

extern "C" void on_anim_enabled_change_cb(void)
{
    storage_set_anim_enabled(g_anim_enabled);
}

extern "C" void on_screen_rotation_change_cb(void)
{
    /* M5StickC panel offset_rotation=2:
     *   setRotation(1) → actual 270° (portrait)
     *   setRotation(3) → actual 90°  (landscape) */
    int16_t gfx_rotation = (g_screen_rotation_level == ORIENTATION_PORTRAIT) ? 1 : 3;
    M5.Display.setRotation(gfx_rotation);
    storage_set_screen_rotation((uint8_t)g_screen_rotation_level);
    hal_display_init();
}

/* ─── 入口 ─── */

void setup()
{
    M5.begin();

    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n=== M5Stick BOOT ===");

    storage_init();
    settings_load_from_storage();

    brightness = g_brightness_level * 10;
    M5.Display.setBrightness((uint8_t)settings_brightness_hw_value());
    g_anim_speed = settings_anim_speed_value();

    /* M5StickC panel offset_rotation=2:
     *   setRotation(1) → actual 270° (portrait)
     *   setRotation(3) → actual 90°  (landscape) */
    int16_t gfx_rotation = (g_screen_rotation_level == ORIENTATION_PORTRAIT) ? 1 : 3;
    M5.Display.setRotation(gfx_rotation);

    xerintosh_ui_driver_init();
    boot_screen_show();
    app_init_ui();
    app_init_managers();

    xerintosh_init_core();
    in_xerintosh = true;
}

void loop()
{
    M5.update();
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
    delay(16);
}

#endif /* NATIVE_TEST */
