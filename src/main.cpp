#include <stdint.h>

#include "app/storage.h"
extern "C" int16_t resolve_rotation_level(uint8_t saved_rot);

extern "C" {
int16_t g_anim_speed = 92;
}
bool wifi_on = true;
bool bt_on = true;

#ifndef NATIVE_TEST

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

static int16_t brightness = 50;
static int16_t brightness_level = 5;
static int16_t anim_speed_level = 5;
static int16_t screen_rotation = 1;
static int16_t screen_rotation_level = 2;

static void on_brightness_change()
{
    brightness = brightness_level * 10;
    uint8_t hw = (uint8_t)((brightness * 255) / 100);
    M5.Display.setBrightness(hw);
    storage_set_brightness(brightness);
}

static void on_anim_speed_change()
{
    g_anim_speed = 40 + anim_speed_level * 5;
    storage_set_anim_speed(g_anim_speed);
}

static void on_anim_enabled_change()
{
    storage_set_anim_enabled(g_anim_enabled);
}

static void on_screen_rotation_change()
{
    screen_rotation = (screen_rotation_level == 1) ? 0 : 1;
    M5.Display.setRotation(screen_rotation);
    storage_set_screen_rotation(screen_rotation_level);
    hal_display_init();
}

static void input_process()
{
    hal_input_update();

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
    {
        astra_selector_go_prev_item();
    }
    else if (event_b == HAL_EVENT_LONG_PRESS)
    {
        if (wifi_mgr_is_waiting_input()) {
            serial_cancel();
        } else if (bt_mgr_is_waiting_input()) {
            serial_cancel();
        }
        astra_selector_exit_current_item();
    }

    if (event_a == HAL_EVENT_SHORT_PRESS)
    {
        astra_selector_go_next_item();
    }
    else if (event_a == HAL_EVENT_LONG_PRESS)
    {
        astra_selector_jump_to_selected_item();
    }
}

void setup()
{
    M5.begin();

    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n=== M5Stick BOOT ===");

    storage_init();
    int16_t saved = storage_get_brightness();
    if (saved >= 0) {
        if (saved >= 1 && saved <= 10) {
            brightness_level = saved;
        } else {
            brightness_level = (saved + 9) / 10;
            if (brightness_level < 1) brightness_level = 1;
            if (brightness_level > 10) brightness_level = 10;
        }
        brightness = brightness_level * 10;
    }
    M5.Display.setBrightness((uint8_t)((brightness * 255) / 100));

    uint8_t saved_anim = storage_get_anim_speed();
    if (saved_anim >= 1 && saved_anim <= 10) {
        anim_speed_level = saved_anim;
    } else if (saved_anim >= 40 && saved_anim <= 95) {
        anim_speed_level = (saved_anim - 40) / 5;
        if (anim_speed_level < 1) anim_speed_level = 1;
        if (anim_speed_level > 10) anim_speed_level = 10;
    }
    g_anim_speed = 40 + anim_speed_level * 5;

    g_anim_enabled = storage_get_anim_enabled();
    uint8_t saved_rot = storage_get_screen_rotation();
    /* Back-compat: old screen_rot values were 0=portrait,1=landscape,2=portrait180,3=landscape270.
       New values are level 1=portrait(0deg), 2=landscape(90deg). Map old landscape (1,3) → 2,
       old portrait (0,2) → 1. If already a new level (1 or 2), keep it. */
    screen_rotation_level = resolve_rotation_level(saved_rot);
    screen_rotation = (screen_rotation_level == 1) ? 0 : 1;
    M5.Display.setRotation(screen_rotation);

    astra_ui_driver_init();

    astra_list_item_t* root = astra_get_root_list();

    astra_list_item_t* item1 = astra_new_list_item("设置", list_icon);
    astra_list_item_t* item2 = astra_new_list_item("关于", user_icon);

    astra_list_item_t* sw1 = astra_new_switch_item("WiFi", &wifi_on, nullptr, wifi_mgr_on_switch_toggle, default_icon);
    astra_list_item_t* sw2 = astra_new_switch_item("蓝牙", &bt_on, nullptr, bt_mgr_on_switch_toggle, default_icon);
    astra_list_item_t* sl1 = astra_new_slider_item("亮度", &brightness_level, 1, 1, 10, nullptr, on_brightness_change, default_icon);
    astra_list_item_t* sw_anim = astra_new_switch_item("动画效果", &g_anim_enabled, nullptr, on_anim_enabled_change, default_icon);
    astra_list_item_t* sl_anim = astra_new_slider_item("动画速度", &anim_speed_level, 1, 1, 10, nullptr, on_anim_speed_change, default_icon);
    astra_list_item_t* sl_rot = astra_new_slider_item("屏幕方向", &screen_rotation_level, 1, 1, 2, nullptr, on_screen_rotation_change, default_icon);

    astra_push_item_to_list(root, item1);
    astra_push_item_to_list(root, item2);
    astra_push_item_to_list(item1, sw1);
    astra_push_item_to_list(item1, sw2);
    astra_push_item_to_list(item1, sl1);
    astra_push_item_to_list(item1, sw_anim);
    astra_push_item_to_list(item1, sl_anim);
    astra_push_item_to_list(item1, sl_rot);

    wifi_mgr_init();
    bt_mgr_init();

    if (wifi_on) wifi_mgr_enable();
    if (bt_on)   bt_mgr_enable();

    astra_init_core();
    in_astra = true;
}

void loop()
{
    M5.update();
    input_process();
    wifi_mgr_update();
    bt_mgr_update();
    hal_display_clear();
    astra_ui_main_core();
    astra_ui_widget_core();

    uint32_t dur_a = hal_input_get_press_duration(HAL_BTN_A);
    uint32_t dur_b = hal_input_get_press_duration(HAL_BTN_B);
    if (dur_a > 0 && dur_a < 500) {
        astra_draw_long_press_hint(dur_a, 500);
    } else if (dur_b > 0 && dur_b < 500) {
        astra_draw_long_press_hint(dur_b, 500);
    }

    hal_display_flush();
    delay(16);
}

#endif /* NATIVE_TEST */
