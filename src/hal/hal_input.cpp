#include "hal_input.h"
#include "hal_system.h"

#ifdef NATIVE_TEST
static bool hal_input_read_raw(hal_button_t btn) {
    (void)btn;
    return false;
}
#else
#include <M5Unified.h>
static bool hal_input_read_raw(hal_button_t btn) {
    if (btn == HAL_BTN_A) return M5.BtnA.isPressed();
    if (btn == HAL_BTN_B) return M5.BtnB.isPressed();
    return false;
}
#endif

#define DEBOUNCE_FRAMES      3
#define SHORT_PRESS_MS       200
#define LONG_PRESS_MS        500
#define LONG_PRESS_REPEAT_MS 100
#define DOUBLE_CLICK_MS      300

static hal_button_state_t g_buttons[HAL_BTN_COUNT];

void hal_input_init(void) {
    for (int i = 0; i < HAL_BTN_COUNT; i++) {
        g_buttons[i].pressed = false;
        g_buttons[i].mode = false;
        g_buttons[i].pressTime = 0;
        g_buttons[i].lastReleaseTime = 0;
        g_buttons[i].debounceCount = 0;
        g_buttons[i].debouncedState = false;
        g_buttons[i].lastRawState = false;
        g_buttons[i].longPressFired = false;
        g_buttons[i].lastRepeatTime = 0;
    }
}

static void hal_input_update_button(hal_button_t btn) {
    hal_button_state_t* state = &g_buttons[btn];
    bool raw = hal_input_read_raw(btn);

    // Debounce: require 3 consecutive same readings
    if (raw == state->lastRawState) {
        if (state->debounceCount < DEBOUNCE_FRAMES) {
            state->debounceCount++;
        }
    } else {
        state->debounceCount = 0;
        state->lastRawState = raw;
    }

    bool newState = (state->debounceCount >= DEBOUNCE_FRAMES) ? state->lastRawState : state->debouncedState;
    bool changed = (newState != state->debouncedState);
    state->debouncedState = newState;

    uint32_t now = hal_get_ticks();

    if (changed) {
        if (newState) {
            // Just pressed
            state->pressTime = now;
            state->longPressFired = false;
            state->lastRepeatTime = now;
            state->pressed = true;
        } else {
            // Just released
            state->pressed = false;
            uint32_t duration = now - state->pressTime;
            if (duration < SHORT_PRESS_MS) {
                // Check for double-click
                if ((now - state->lastReleaseTime) < DOUBLE_CLICK_MS) {
                    state->mode = !state->mode; // toggle mode
                    state->lastReleaseTime = 0; // reset to prevent triple-click
                } else {
                    state->lastReleaseTime = now;
                }
            }
        }
    } else {
        // No change in debounced state
        if (newState) {
            // Still held
            uint32_t duration = now - state->pressTime;
            if (duration >= LONG_PRESS_MS) {
                if (!state->longPressFired) {
                    state->longPressFired = true;
                    state->lastRepeatTime = now;
                } else if ((now - state->lastRepeatTime) >= LONG_PRESS_REPEAT_MS) {
                    state->lastRepeatTime = now;
                }
            }
        } else {
            // Still released: decay double-click window
            if (state->lastReleaseTime != 0 && (now - state->lastReleaseTime) >= DOUBLE_CLICK_MS) {
                state->lastReleaseTime = 0;
            }
        }
    }
}

void hal_input_update(void) {
    hal_input_update_button(HAL_BTN_A);
    hal_input_update_button(HAL_BTN_B);
}

hal_event_t hal_input_get_event(hal_button_t btn) {
    if (btn >= HAL_BTN_COUNT) return HAL_EVENT_NONE;
    hal_button_state_t* state = &g_buttons[btn];

    if (!state->debouncedState) {
        // Released: check for pending short press (single click that wasn't part of double-click)
        if (state->lastReleaseTime != 0) {
            uint32_t now = hal_get_ticks();
            if ((now - state->lastReleaseTime) >= DOUBLE_CLICK_MS) {
                state->lastReleaseTime = 0;
                return HAL_EVENT_SHORT_PRESS;
            }
        }
        return HAL_EVENT_NONE;
    }

    // Button is currently pressed
    uint32_t now = hal_get_ticks();
    uint32_t duration = now - state->pressTime;

    if (duration >= LONG_PRESS_MS) {
        if (state->longPressFired && (now - state->lastRepeatTime) >= LONG_PRESS_REPEAT_MS) {
            state->lastRepeatTime = now;
            return HAL_EVENT_LONG_PRESS;
        }
        if (!state->longPressFired) {
            state->longPressFired = true;
            state->lastRepeatTime = now;
            return HAL_EVENT_LONG_PRESS;
        }
    }

    return HAL_EVENT_NONE;
}

bool hal_input_is_pressed(hal_button_t btn) {
    if (btn >= HAL_BTN_COUNT) return false;
    return g_buttons[btn].debouncedState;
}

bool hal_input_get_mode(hal_button_t btn) {
    if (btn >= HAL_BTN_COUNT) return false;
    return g_buttons[btn].mode;
}
