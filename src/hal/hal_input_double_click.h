/**
 * @file   hal_input_double_click.h
 * @brief  双击检测状态机头文件
 * @details 纯 C 实现，不依赖硬件环境。将双击检测逻辑从 hal_input.cpp 中提取出来，
 *          以便在 native 环境下进行单元测试。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_INPUT_DOUBLE_CLICK_H
#define HAL_INPUT_DOUBLE_CLICK_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define DOUBLE_CLICK_WINDOW_MS 300  /* 双击窗口期（毫秒） */
#define LONG_PRESS_DURATION_MS 500  /* 长按触发阈值（毫秒） */

/* ═══ 状态结构 ═══ */

/**
 * @brief 双击检测状态机结构体
 */
typedef struct {
    bool pressed;                  /* 是否处于按下态 */
    uint32_t press_time;           /* 本次按下起始时间戳 */
    bool long_fired;               /* 长按事件是否已触发 */
    uint32_t press_duration_ms;    /* 当前按下持续时间 */
    uint32_t last_release_ms;      /* 上次释放的时间戳 */
    bool pending_short_press;      /* 有待处理的短按（等待窗口期确认） */
    bool in_double_click_sequence; /* 当前处于双击序列中的第二次按下 */
} hal_input_dc_state_t;

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化双击检测状态机
 */
void hal_input_dc_init(hal_input_dc_state_t *st);

/* ═══ 状态机处理 ═══ */

/**
 * @brief  处理按键边沿信号，返回对应事件
 * @param  st           状态机指针
 * @param  was_pressed  本帧是否检测到按下边沿
 * @param  was_released 本帧是否检测到释放边沿
 * @param  now_ms       当前时间戳（毫秒）
 * @return 事件类型；无事件时返回 HAL_EVENT_NONE
 * @note   时序规则：
 *         1. 首次按下释放 → 记录时间，进入等待窗口期
 *         2. 窗口期内再次按下释放 → 返回 DOUBLE_CLICK
 *         3. 窗口期超时 → 返回 SHORT_PRESS
 *         4. 按下持续超过 LONG_PRESS_DURATION_MS → 返回 LONG_PRESS
 *         5. 长按优先级高于双击：若第二次按下达到长按阈值，触发长按而非双击
 */
hal_event_t hal_input_dc_process(hal_input_dc_state_t *st,
                                  bool was_pressed,
                                  bool was_released,
                                  uint32_t now_ms);

/**
 * @brief  简单按键事件检测（无双击检测，即时响应）
 * @param  st           状态机指针
 * @param  was_pressed  本帧是否检测到按下边沿
 * @param  was_released 本帧是否检测到释放边沿
 * @param  now_ms       当前时间戳（毫秒）
 * @return 事件类型；无事件时返回 HAL_EVENT_NONE
 * @note   与 hal_input_dc_process 不同，此函数释放时立即返回 SHORT_PRESS，
 *         无 300ms 双击窗口期延迟。适用于菜单导航等不需要双击的场景。
 *
 * 中文伪代码拆解：
 *
 * 函数 简单按键处理(状态, 按下边沿, 释放边沿, 当前时间) {
 *     // 第一步：按下边沿 → 记录时间，重置长按标记
 *     if (按下边沿) {
 *         标记按下态
 *         记录按下时间
 *         重置长按已触发
 *     }
 *
 *     // 第二步：释放边沿 → 非长按则立即返回短按
 *     if (释放边沿) {
 *         标记释放态
 *         if (长按未触发) {
 *             重置所有标志
 *             返回 短按事件
 *         }
 *         重置所有标志
 *     }
 *
 *     // 第三步：持续按下中检测长按
 *     if (按下态 且 长按未触发) {
 *         计算持续时间
 *         if (持续时间 >= 阈值) {
 *             标记长按已触发
 *             返回 长按事件
 *         }
 *     }
 *
 *     返回 无事件
 * }
 */
hal_event_t hal_input_simple_process(hal_input_dc_state_t *st,
                                      bool was_pressed,
                                      bool was_released,
                                      uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_INPUT_DOUBLE_CLICK_H */
