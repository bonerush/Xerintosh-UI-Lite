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

/**
 * @brief 按键状态结构体
 * @note  用于输入模块内部维护每个按键的消抖和时序状态
 */
typedef struct {
    bool pressed;            /* 当前是否处于按下态 */
    bool mode;               /* 0 = mode1, 1 = mode2，由双击切换 */
    uint32_t pressTime;      /* 本次按下的起始时间戳 */
    uint32_t lastReleaseTime;/* 上次释放的时间戳 */
    uint8_t debounceCount;   /* 消抖计数器 */
    bool debouncedState;     /* 消抖后的稳定状态 */
    bool lastRawState;       /* 上一次原始状态 */
    bool longPressFired;     /* 长按事件是否已触发 */
    uint32_t lastRepeatTime; /* 上次连发时间 */
    uint32_t press_duration_ms; /* 当前按下的持续时间 */
} hal_button_state_t;

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
 * @brief  获取按键的模式状态
 * @param  btn 按键标识
 * @return true  mode2
 * @return false mode1
 */
extern bool hal_input_get_mode(hal_button_t btn);

/**
 * @brief  获取按键当前按下的持续时间
 * @param  btn 按键标识
 * @return 持续时间（毫秒）
 */
extern uint32_t hal_input_get_press_duration(hal_button_t btn);

#ifdef __cplusplus
}
#endif

#endif /* HAL_INPUT_H */
