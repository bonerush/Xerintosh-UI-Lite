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
#include "hal_input_double_click.h"

/* ═══ 双击开关（全局状态）═══ */

static bool g_double_click_enabled = false;

void hal_input_set_double_click_enabled(bool enabled) {
    g_double_click_enabled = enabled;
}

bool hal_input_is_double_click_enabled(void) {
    return g_double_click_enabled;
}

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

/**
 * @brief 内部按键状态结构
 * @note  使用 hal_input_double_click.h 中的双击检测状态机
 */
struct btn_state {
    hal_input_dc_state_t dc;  /* 双击检测状态机 */
};

static struct btn_state g_btn_a;  /* 按键 A 状态 */
static struct btn_state g_btn_b;  /* 按键 B 状态 */

/**
 * @brief 初始化按键状态
 */
void hal_input_init(void) {
    hal_input_dc_init(&g_btn_a.dc);
    hal_input_dc_init(&g_btn_b.dc);
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
 * @note   根据 g_double_click_enabled 选择简单状态机或双击状态机
 */
static hal_event_t check_button_event(struct btn_state *st, bool wasPressed, bool wasReleased)
{
    uint32_t now = millis();
    if (g_double_click_enabled) {
        return hal_input_dc_process(&st->dc, wasPressed, wasReleased, now);
    }
    return hal_input_simple_process(&st->dc, wasPressed, wasReleased, now);
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
        if (pressed && st->dc.pressed) {
            st->dc.press_duration_ms = millis() - st->dc.press_time;
        }
        return pressed;
    }
    if (btn == HAL_BTN_B) {
        st = &g_btn_b;
        bool pressed = M5.BtnB.isPressed();
        if (pressed && st->dc.pressed) {
            st->dc.press_duration_ms = millis() - st->dc.press_time;
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
    return st->dc.press_duration_ms;
}

#endif
