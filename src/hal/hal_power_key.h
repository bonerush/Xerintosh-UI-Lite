/**
 * @file   hal_power_key.h
 * @brief  HAL 电源键驱动头文件
 * @details 定义 AXP192 电源键事件类型及电源键检测 API。
 *          电源键仅连接到 AXP192 电源管理芯片，需通过 I2C 轮询检测。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_POWER_KEY_H
#define HAL_POWER_KEY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define PWR_KEY_LONG_PRESS_MS  1500  /* 长按阈值（毫秒） */
#define PWR_KEY_HOLD_REPEAT_MS  500  /* 持续按住重复间隔（毫秒） */

/* ═══ 类型定义 ═══ */

/**
 * @brief 电源键事件类型枚举
 */
typedef enum {
    HAL_PWR_KEY_NONE = 0,      /* 无事件 */
    HAL_PWR_KEY_SHORT_PRESS,   /* 短按（< 1.5s） */
    HAL_PWR_KEY_LONG_PRESS,    /* 长按（≥ 1.5s） */
    HAL_PWR_KEY_HOLD           /* 持续按住（每 500ms 触发一次） */
} hal_pwr_key_event_t;

/* ═══ 操作函数 ═══ */

/**
 * @brief  获取电源键事件（非阻塞，每帧调用）
 * @return 事件类型；无事件时返回 HAL_PWR_KEY_NONE
 * @note   轮询 AXP192 寄存器检测电源键状态变化，
 *         每次调用最多返回一个事件，消费后自动清除
 */
extern hal_pwr_key_event_t hal_power_key_get_event(void);

/**
 * @brief  获取当前按住时长（毫秒）
 * @return 按住时长；未按下时返回 0
 */
extern uint32_t hal_power_key_get_hold_duration_ms(void);

/**
 * @brief  检查电源键是否正在被按住
 * @return true  按住
 * @return false 未按下
 */
extern bool hal_power_key_is_pressed(void);

/* ═══ 测试钩子（仅 NATIVE_TEST） ═══ */

#ifdef NATIVE_TEST
/**
 * @brief  注入电源键按下/释放状态（测试代码调用）
 * @param  pressed true=按下，false=释放
 * @param  now_ms  当前时间戳（毫秒）
 */
void hal_power_key_test_inject(bool pressed, uint32_t now_ms);

/**
 * @brief  重置电源键状态机（测试代码调用）
 */
void hal_power_key_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_POWER_KEY_H */
