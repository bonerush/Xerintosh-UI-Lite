/**
 * @file   hal_input.cpp
 * @brief  HAL 输入层实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有输入函数返回空状态（桩实现）
 *          - 硬件环境时：基于 M5Unified 按键事件，实现消抖、短按/长按检测
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_input.h"
#include "hal_system.h"

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：输入桩 ═══ */

/**
 * @brief 初始化输入（空操作）
 */
void hal_input_init(void) {}

/**
 * @brief 更新输入状态（空操作）
 */
void hal_input_update(void) {}

/**
 * @brief 获取按键事件（始终返回无事件）
 */
hal_event_t hal_input_get_event(hal_button_t btn) {
    (void)btn;
    return HAL_EVENT_NONE;
}

/**
 * @brief 查询按键是否按下（始终返回 false）
 */
bool hal_input_is_pressed(hal_button_t btn) {
    (void)btn;
    return false;
}

/**
 * @brief 获取按键模式（始终返回 false）
 */
bool hal_input_get_mode(hal_button_t btn) {
    (void)btn;
    return false;
}

#else

/* ═══ 硬件环境：M5Unified 按键处理 ═══ */

#include <M5Unified.h>

#define LONG_PRESS_DURATION_MS  500  /* 长按触发阈值（毫秒） */

/**
 * @brief 内部按键状态结构（简化版，仅保留必要字段）
 */
struct btn_state {
    bool pressed;              /* 是否处于按下态 */
    uint32_t press_time;       /* 按下起始时间 */
    bool long_fired;           /* 长按事件是否已触发 */
    uint32_t press_duration_ms; /* 当前按下持续时间 */
};

static struct btn_state g_btn_a = {false, 0, false, 0};  /* 按键 A 状态 */
static struct btn_state g_btn_b = {false, 0, false, 0};  /* 按键 B 状态 */

/**
 * @brief 初始化按键状态
 */
void hal_input_init(void) {
    g_btn_a = {false, 0, false, 0};
    g_btn_b = {false, 0, false, 0};
}

/**
 * @brief 更新输入状态
 * @note  M5.update() 在 main.cpp 的 loop() 中先行调用，此处仅作占位
 */
void hal_input_update(void) {
}

/**
 * @brief  检测单个按键的事件状态转换
 * @param  st          按键状态指针
 * @param  wasPressed  本帧是否检测到按下边沿
 * @param  wasReleased 本帧是否检测到释放边沿
 * @return 事件类型
 */
static hal_event_t check_button_event(struct btn_state *st, bool wasPressed, bool wasReleased)
{
  if (wasPressed)
  {
    st->pressed = true;
    st->press_time = millis();
    st->long_fired = false;
    st->press_duration_ms = 0;
  }
  if (wasReleased)
  {
    st->pressed = false;
    st->press_duration_ms = 0;
    if (!st->long_fired)
    {
      return HAL_EVENT_SHORT_PRESS;
    }
  }
  if (st->pressed && !st->long_fired)
  {
    st->press_duration_ms = millis() - st->press_time;
    if (st->press_duration_ms >= LONG_PRESS_DURATION_MS)
    {
      st->long_fired = true;
      return HAL_EVENT_LONG_PRESS;
    }
  }
  return HAL_EVENT_NONE;
}

/**
 * @brief 获取指定按键的当前事件
 */
hal_event_t hal_input_get_event(hal_button_t btn)
{
  struct btn_state *st = NULL;
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

/**
 * @brief 查询指定按键是否正处于按下状态
 */
bool hal_input_is_pressed(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) {
        st = &g_btn_a;
        bool pressed = M5.BtnA.isPressed();
        if (pressed && st->pressed) {
            st->press_duration_ms = millis() - st->press_time;
        }
        return pressed;
    }
    if (btn == HAL_BTN_B) {
        st = &g_btn_b;
        bool pressed = M5.BtnB.isPressed();
        if (pressed && st->pressed) {
            st->press_duration_ms = millis() - st->press_time;
        }
        return pressed;
    }
    return false;
}

/**
 * @brief 获取按键的模式状态（当前未实现，预留接口）
 */
bool hal_input_get_mode(hal_button_t btn) {
    (void)btn;
    return false;
}

/**
 * @brief 获取按键当前按下的持续时间
 */
uint32_t hal_input_get_press_duration(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) st = &g_btn_a;
    else if (btn == HAL_BTN_B) st = &g_btn_b;
    else return 0;
    return st->press_duration_ms;
}

#endif
