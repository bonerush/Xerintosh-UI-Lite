/**
 * @file   hal_input.h
 * @brief  HAL 输入层头文件
 * @details 定义按键枚举、事件类型、按键状态结构体及输入处理 API。
 *          支持双按键（A/B）的短按、长按和双击事件检测。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_INPUT_H
#define HAL_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 类型定义 ═══ */

/**
 * @brief 按键标识枚举
 */
typedef enum {
    HAL_BTN_A = 0,  /* 按键 A（M5StickC 侧键） */
    HAL_BTN_B = 1,  /* 按键 B（M5StickC 主键） */
    HAL_BTN_COUNT   /* 按键总数 */
} hal_button_t;

/**
 * @brief 按键事件类型枚举
 */
typedef enum {
    HAL_EVENT_NONE = 0,       /* 无事件 */
    HAL_EVENT_SHORT_PRESS,    /* 短按 */
    HAL_EVENT_LONG_PRESS,     /* 长按 */
    HAL_EVENT_DOUBLE_CLICK    /* 双击 */
} hal_event_t;

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化输入子系统
 */
extern void hal_input_init(void);

/* ═══ 操作函数 ═══ */

/**
 * @brief  更新输入状态（应在主循环每帧调用）
 * @note   native 环境为空操作；硬件环境依赖 M5.update() 在 main 循环中先行调用
 */
extern void hal_input_update(void);

/**
 * @brief  获取指定按键的当前事件
 * @param  btn 按键标识
 * @return 事件类型；无事件时返回 HAL_EVENT_NONE
 */
extern hal_event_t hal_input_get_event(hal_button_t btn);

/**
 * @brief  查询指定按键是否正处于按下状态
 * @param  btn 按键标识
 * @return true  按下
 * @return false 释放
 */
extern bool hal_input_is_pressed(hal_button_t btn);

/**
 * @brief  获取按键当前按下的持续时间
 * @param  btn 按键标识
 * @return 持续时间（毫秒）
 */
extern uint32_t hal_input_get_press_duration(hal_button_t btn);

/* ═══ 双击开关 ═══ */

/**
 * @brief  设置是否启用双击检测
 * @param  enabled true=启用双击检测（含 300ms 窗口延迟），false=禁用（即时短按响应）
 * @note   默认禁用。菜单模式应禁用双击避免延迟；App 模式可启用以支持双击操作。
 */
extern void hal_input_set_double_click_enabled(bool enabled);

/**
 * @brief  重置所有按键的事件状态机
 * @note   在 user_item 的 init/exit 中调用，清除跨模式的残留状态，
 *         防止进入/退出 App 时按键边沿丢失导致的僵死状态。
 */
extern void hal_input_reset_events(void);

/* ═══ 测试钩子（仅 NATIVE_TEST） ═══ */

#ifdef NATIVE_TEST
/**
 * @brief  注入测试按键事件（测试代码调用）
 * @param  btn 按键标识
 * @param  ev  事件类型
 */
void hal_test_inject_event(hal_button_t btn, hal_event_t ev);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_INPUT_H */
