/**
 * @file   app_input.c
 * @brief  App per-frame input processing implementation
 * @details Maps hardware button events to UI selector navigation,
 *          and dispatches module state machines (power key popup, WiFi popup, flasher force release, etc.).
 *
 * @copyright Copyright (c) 2026
 */

#include "app_input.h"

#include "app_state.h"
#include "settings/settings.h"
#include "wifi/wifi_manager.h"
#include "serial_input/serial_input.h"
#include "app/shutdown/power_key_popup.h"
#include "app/flasher/flasher_menu.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"

/* WiFi popup refresh (defined in wifi_manager.cpp, called by UI task every frame) */
extern void wifi_popup_refresh(void);

/**
 * @brief Process button input events, mapped to UI selector operations
 * @note  Button mapping:
 *        - Btn B short press: selector up (previous item)
 *        - Btn B long press: exit current item / cancel input
 *        - Btn A short press: selector down (next item)
 *        - Btn A long press: confirm/enter current item
 */
void app_input_process(void)
{
    /* Regardless of mode, hal_input_update() must be called every frame
       to ensure GPIO button edge flags are refreshed.
       user_item internally does not handle framework navigation, but user_item's
       own loop calls hal_input_get_event() to read buttons. */
    hal_input_update();

    /* Update power key popup (detect A+B dual-key shutdown event) */
    power_key_popup_update();

    /* Refresh WiFi popup (cross-task popup, pushed every frame to keep display) */
    wifi_popup_refresh();

    /* Flasher pin menu: delayed popup + force release state machine */
    flasher_menu_process_input();

    /* In dual-key hold mode, isolate all normal button events to prevent UI jitter */
    if (power_key_popup_is_dual_active()) {
        return;
    }

    /* If inside user_item, framework input is taken over by the App itself */
    if (xerintosh_is_in_user_item()) {
        return;
    }

    /* Block framework input during enter/exit animations to avoid accidental triggers */
    if (!g_xerintosh_exit_animation_finished) {
        return;
    }

    /* Skip framework navigation when flasher force release state machine is active */
    if (flasher_menu_is_active()) {
        return;
    }

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_prev_item();
    }
    else if (event_b == HAL_EVENT_LONG_PRESS)
    {
        /* If WiFi is waiting for serial input, cancel input first */
        if (wifi_mgr_is_waiting_input()) {
            serial_cancel();
        }
        xerintosh_selector_exit_current_item();
    }

    if (event_a == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_next_item();
    }
    else if (event_a == HAL_EVENT_LONG_PRESS)
    {
        xerintosh_selector_jump_to_selected_item();
    }
}
