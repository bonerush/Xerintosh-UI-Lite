#ifndef HAL_INPUT_H
#define HAL_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 类型定义 ─── */

typedef enum {
    HAL_BTN_A = 0,
    HAL_BTN_B = 1,
    HAL_BTN_COUNT
} hal_button_t;

typedef enum {
    HAL_EVENT_NONE = 0,
    HAL_EVENT_SHORT_PRESS,
    HAL_EVENT_LONG_PRESS,
    HAL_EVENT_DOUBLE_CLICK
} hal_event_t;

typedef struct {
    bool pressed;
    bool mode;           /* 0 = mode1, 1 = mode2, toggled by double-click */
    uint32_t pressTime;
    uint32_t lastReleaseTime;
    uint8_t debounceCount;
    bool debouncedState;
    bool lastRawState;
    bool longPressFired;
    uint32_t lastRepeatTime;
    uint32_t press_duration_ms;
} hal_button_state_t;

/* ─── 生命周期 ─── */

extern void hal_input_init(void);

/* ─── 操作函数 ─── */

extern void hal_input_update(void);
extern hal_event_t hal_input_get_event(hal_button_t btn);
extern bool hal_input_is_pressed(hal_button_t btn);
extern bool hal_input_get_mode(hal_button_t btn);
extern uint32_t hal_input_get_press_duration(hal_button_t btn);

#ifdef __cplusplus
}
#endif

#endif /* HAL_INPUT_H */
