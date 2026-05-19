#include <M5Unified.h>
#include <M5GFX.h>

extern "C" {
#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui/ui_draw_driver.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "app/storage.h"
}
#include "app/serial_input.h"
#include "app/wifi_manager.h"
#include "app/bt_manager.h"

static int16_t brightness = 50;
uint8_t g_anim_speed = 92;
bool wifi_on = false;
bool bt_on = false;

static void on_brightness_change()
{
    uint8_t hw = (uint8_t)((brightness * 255) / 100);
    M5.Display.setBrightness(hw);
    storage_set_brightness(brightness);
}

static void on_anim_speed_change()
{
    storage_set_anim_speed(g_anim_speed);
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
    M5.Display.setRotation(1);

    storage_init();
    int16_t saved = storage_get_brightness();
    if (saved >= 0) brightness = saved;
    M5.Display.setBrightness((uint8_t)((brightness * 255) / 100));

    uint8_t saved_anim = storage_get_anim_speed();
    if (saved_anim >= 50 && saved_anim <= 95) g_anim_speed = saved_anim;

    astra_ui_driver_init();

    astra_list_item_t* root = astra_get_root_list();

    astra_list_item_t* item1 = astra_new_list_item("设置", list_icon);
    astra_list_item_t* item2 = astra_new_list_item("关于", user_icon);

    astra_list_item_t* sw1 = astra_new_switch_item("WiFi", &wifi_on, nullptr, wifi_mgr_on_switch_toggle, default_icon);
    astra_list_item_t* sw2 = astra_new_switch_item("蓝牙", &bt_on, nullptr, bt_mgr_on_switch_toggle, default_icon);
    astra_list_item_t* sl1 = astra_new_slider_item("亮度", &brightness, 5, 0, 100, nullptr, on_brightness_change, default_icon);
    astra_list_item_t* sl_anim = astra_new_slider_item("动画速度", (int16_t*)&g_anim_speed, 1, 50, 95, nullptr, on_anim_speed_change, default_icon);

    astra_push_item_to_list(root, item1);
    astra_push_item_to_list(root, item2);
    astra_push_item_to_list(item1, sw1);
    astra_push_item_to_list(item1, sw2);
    astra_push_item_to_list(item1, sl1);
    astra_push_item_to_list(item1, sl_anim);

    wifi_mgr_init();
    bt_mgr_init();

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
