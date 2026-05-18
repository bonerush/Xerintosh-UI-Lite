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
}

static void input_process()
{
    hal_input_update();

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_a == HAL_EVENT_SHORT_PRESS)
    {
        astra_selector_go_prev_item();
    }
    else if (event_a == HAL_EVENT_LONG_PRESS)
    {
        astra_selector_exit_current_item();
    }

    if (event_b == HAL_EVENT_SHORT_PRESS)
    {
        astra_selector_go_next_item();
    }
    else if (event_b == HAL_EVENT_LONG_PRESS)
    {
        astra_selector_jump_to_selected_item();
    }
}

void setup()
{
    M5.begin();
    M5.Display.setBrightness(255);
    M5.Display.setRotation(1);

    astra_ui_driver_init();

    astra_list_item_t* root = astra_get_root_list();

    astra_list_item_t* item1 = astra_new_list_item("Settings", list_icon);
    astra_list_item_t* item2 = astra_new_list_item("About", user_icon);

    static bool wifi_on = false;
    astra_list_item_t* sw1 = astra_new_switch_item("WiFi", &wifi_on, nullptr, nullptr, default_icon);

    static int16_t brightness = 50;
    astra_list_item_t* sl1 = astra_new_slider_item("Brightness", &brightness, 5, 0, 100, nullptr, nullptr, default_icon);

    astra_push_item_to_list(root, item1);
    astra_push_item_to_list(root, item2);
    astra_push_item_to_list(item1, sw1);
    astra_push_item_to_list(item1, sl1);

    astra_init_core();
    in_astra = true;
}

void loop()
{
    M5.update();
    input_process();
    hal_display_clear();
    astra_ui_main_core();
    astra_ui_widget_core();
    hal_display_flush();
    delay(16);
}
