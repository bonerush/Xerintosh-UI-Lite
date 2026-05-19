#include "hal_input.h"
#include "hal_system.h"

#ifdef NATIVE_TEST

void hal_input_init(void) {}
void hal_input_update(void) {}

hal_event_t hal_input_get_event(hal_button_t btn) {
    (void)btn;
    return HAL_EVENT_NONE;
}

bool hal_input_is_pressed(hal_button_t btn) {
    (void)btn;
    return false;
}

bool hal_input_get_mode(hal_button_t btn) {
    (void)btn;
    return false;
}

#else

#include <M5Unified.h>

#define LONG_PRESS_DURATION_MS  500

struct btn_state {
    bool pressed;
    uint32_t press_time;
    bool long_fired;
};

static struct btn_state g_btn_a = {false, 0, false};
static struct btn_state g_btn_b = {false, 0, false};

void hal_input_init(void) {
    g_btn_a = {false, 0, false};
    g_btn_b = {false, 0, false};
}

void hal_input_update(void) {
    // M5.update() is called in main.cpp loop() before input_process()
    // M5Unified handles debounce internally; we just track edge events here
}

static hal_event_t check_button_event(struct btn_state *st, bool wasPressed, bool wasReleased)
{
  if (wasPressed)
  {
    st->pressed = true;
    st->press_time = millis();
    st->long_fired = false;
  }
  if (wasReleased)
  {
    st->pressed = false;
    if (!st->long_fired)
    {
      return HAL_EVENT_SHORT_PRESS;
    }
  }
  if (st->pressed && !st->long_fired)
  {
    if (millis() - st->press_time >= LONG_PRESS_DURATION_MS)
    {
      st->long_fired = true;
      return HAL_EVENT_LONG_PRESS;
    }
  }
  return HAL_EVENT_NONE;
}

hal_event_t hal_input_get_event(hal_button_t btn)
{
  struct btn_state *st = nullptr;
  if (btn == HAL_BTN_A) st = &g_btn_a;
  else if (btn == HAL_BTN_B) st = &g_btn_b;
  else return HAL_EVENT_NONE;

  if (btn == HAL_BTN_A)
  {
    return check_button_event(st, M5.BtnA.wasPressed(), M5.BtnA.wasReleased());
  }
  else
  {
    return check_button_event(st, M5.BtnB.wasPressed(), M5.BtnB.wasReleased());
  }
}

bool hal_input_is_pressed(hal_button_t btn) {
    if (btn == HAL_BTN_A) return M5.BtnA.isPressed();
    if (btn == HAL_BTN_B) return M5.BtnB.isPressed();
    return false;
}

bool hal_input_get_mode(hal_button_t btn) {
    (void)btn;
    return false;
}

#endif
