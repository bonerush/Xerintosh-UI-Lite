/**
 * @file   hal_input.cpp
 * @brief  HAL 输入层实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有输入函数返回空状态（桩实现）
 *          - 硬件环境时：基于 ESP-IDF GPIO 直接读取按键电平，实现消抖、短按/长按检测
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

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：输入桩 ═══ */

static hal_event_t g_test_events[HAL_BTN_COUNT];
static bool g_test_event_pending[HAL_BTN_COUNT];

/**
 * @brief 初始化输入（空操作）
 */
void hal_input_init(void) {}

/**
 * @brief 更新输入状态（空操作）
 */
void hal_input_update(void) {}

/**
 * @brief 获取按键事件（优先返回测试注入事件）
 */
hal_event_t hal_input_get_event(hal_button_t btn) {
    if (btn >= HAL_BTN_COUNT) return HAL_EVENT_NONE;
    if (g_test_event_pending[btn]) {
        g_test_event_pending[btn] = false;
        return g_test_events[btn];
    }
    return HAL_EVENT_NONE;
}

/**
 * @brief  注入测试按键事件（测试代码调用）
 */
void hal_test_inject_event(hal_button_t btn, hal_event_t ev)
{
    if (btn < HAL_BTN_COUNT) {
        g_test_events[btn] = ev;
        g_test_event_pending[btn] = true;
    }
}

/**
 * @brief  重置所有按键的事件状态机（native 桩）
 */
void hal_input_reset_events(void)
{
    for (int i = 0; i < HAL_BTN_COUNT; i++) {
        g_test_event_pending[i] = false;
    }
}

/**
 * @brief 查询按键是否按下（始终返回 false）
 */
bool hal_input_is_pressed(hal_button_t btn) {
    (void)btn;
    return false;
}

/**
 * @brief 获取按键当前按下的持续时间（始终返回 0）
 */
uint32_t hal_input_get_press_duration(hal_button_t btn) {
    (void)btn;
    return 0;
}

#else

/* ═══ 硬件环境：ESP-IDF GPIO 按键处理 ═══ */

#include "driver/gpio.h"

#define BOOT_INPUT_GUARD_MS  300  /* 启动后首 300ms 忽略输入，防止 GPIO 上电毛刺 */

static uint32_t g_boot_time_ms = 0;  /* hal_input_init 被调用时的毫秒时间戳 */

/**
 * @brief M5Stick-C 按键 GPIO 映射
 * @note  参考 M5Unified：M5StickC 的 BtnA 对应 GPIO37，BtnB 对应 GPIO39。
 *        之前使用 GPIO36/37 的映射会导致返回键（BtnB）失效并误触发进入 App。
 */
static const gpio_num_t g_btn_gpio[HAL_BTN_COUNT] = {
    [HAL_BTN_A] = GPIO_NUM_37,  /* 侧键（M5Unified BtnA） */
    [HAL_BTN_B] = GPIO_NUM_39,  /* 主键/返回键（M5Unified BtnB） */
};

/**
 * @brief 内部按键状态结构
 * @note  使用 hal_input_double_click.h 中的双击检测状态机
 */
struct btn_state {
    hal_input_dc_state_t dc;  /* 双击检测状态机 */
    bool prev_raw;            /* 上一次的原始 GPIO 电平 */
};

static struct btn_state g_btn_a;  /* 按键 A 状态 */
static struct btn_state g_btn_b;  /* 按键 B 状态 */

/**
 * @brief 读取按键原始电平
 * @return true 表示低电平（按下），false 表示高电平（释放）
 * @note  M5Stick-C 按键为低电平有效。
 */
static bool btn_read_raw(hal_button_t btn) {
    if (btn >= HAL_BTN_COUNT) return false;
    return gpio_get_level(g_btn_gpio[btn]) == 0;
}

/**
 * @brief 初始化按键状态
 */
void hal_input_init(void) {
    hal_input_dc_init(&g_btn_a.dc);
    hal_input_dc_init(&g_btn_b.dc);
    g_btn_a.prev_raw = false;
    g_btn_b.prev_raw = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = ((1ULL << GPIO_NUM_37) | (1ULL << GPIO_NUM_39)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    g_boot_time_ms = hal_get_ticks_ms();
}

/**
 * @brief 更新输入状态
 * @note  ESP-IDF 下直接读取 GPIO，无需额外刷新。
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
    uint32_t now = hal_get_ticks_ms();
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
  /* 启动保护：忽略开机后首 300ms 内的所有按键事件，防止 GPIO 上电毛刺触发 LONG_PRESS */
  if (hal_get_ticks_ms() - g_boot_time_ms < BOOT_INPUT_GUARD_MS) {
      return HAL_EVENT_NONE;
  }

  struct btn_state *st = NULL;
  if (btn == HAL_BTN_A) st = &g_btn_a;
  else if (btn == HAL_BTN_B) st = &g_btn_b;
  else return HAL_EVENT_NONE;

  bool raw = btn_read_raw(btn);
  bool wasPressed = raw && !st->prev_raw;
  bool wasReleased = !raw && st->prev_raw;
  st->prev_raw = raw;

  return check_button_event(st, wasPressed, wasReleased);
}

/**
 * @brief 查询指定按键是否正处于按下状态
 */
bool hal_input_is_pressed(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) {
        st = &g_btn_a;
    } else if (btn == HAL_BTN_B) {
        st = &g_btn_b;
    } else {
        return false;
    }

    bool pressed = btn_read_raw(btn);
    if (pressed && st->dc.pressed) {
        st->dc.press_duration_ms = hal_get_ticks_ms() - st->dc.press_time;
    }
    return pressed;
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

/**
 * @brief  重置所有按键的事件状态机
 * @note   在 user_item 的 init/exit 中调用，清除跨模式的残留状态。
 *         同时把 prev_raw 同步为当前 GPIO 电平，避免 reset 后下一次
 *         hal_input_get_event 因旧边沿状态而误判出短按/释放事件。
 */
void hal_input_reset_events(void) {
    hal_input_dc_init(&g_btn_a.dc);
    hal_input_dc_init(&g_btn_b.dc);
    g_btn_a.prev_raw = btn_read_raw(HAL_BTN_A);
    g_btn_b.prev_raw = btn_read_raw(HAL_BTN_B);
}

#endif
