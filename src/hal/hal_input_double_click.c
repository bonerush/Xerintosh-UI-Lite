/**
 * @file   hal_input_double_click.c
 * @brief  双击检测状态机实现
 * @details 纯 C 实现，将双击检测逻辑从 hal_input.cpp 中提取出来，
 *          以便在 native 环境下进行单元测试。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_input_double_click.h"

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化双击检测状态机
 */
void hal_input_dc_init(hal_input_dc_state_t *st)
{
    if (!st) return;
    st->pressed = false;
    st->press_time = 0;
    st->long_fired = false;
    st->press_duration_ms = 0;
    st->last_release_ms = 0;
    st->pending_short_press = false;
    st->in_double_click_sequence = false;
}

/* ═══ 状态机处理 ═══ */

/**
 * @brief  处理按键边沿信号，返回对应事件
 * @param  st           状态机指针
 * @param  was_pressed  本帧是否检测到按下边沿
 * @param  was_released 本帧是否检测到释放边沿
 * @param  now_ms       当前时间戳（毫秒）
 * @return 事件类型；无事件时返回 HAL_EVENT_NONE
 *
 * 中文伪代码拆解：
 *
 * 函数 处理按键事件(状态, 按下边沿, 释放边沿, 当前时间) {
 *     // 第一步：检查是否有超时等待的短按
 *     if (有待处理的短按 且 距离上次释放超过窗口期) {
 *         清除待处理标记
 *         返回 短按事件
 *     }
 *
 *     // 第二步：处理按下边沿
 *     if (检测到按下边沿) {
 *         标记为按下态
 *         记录按下时间
 *         重置长按已触发标记
 *     }
 *
 *     // 第三步：处理释放边沿
 *     if (检测到释放边沿) {
 *         标记为释放态
 *         if (长按未触发) {
 *             if (有待处理的短按) {
 *                 // 在窗口期内再次释放 → 双击
 *                 清除待处理标记
 *                 返回 双击事件
 *             }
 *             // 第一次释放，记录时间，等待窗口期
 *             设置待处理短按标记
 *         } else {
 *             // 长按后释放，重置所有状态
 *             清除长按标记
 *             清除待处理标记
 *         }
 *     }
 *
 *     // 第四步：检测长按
 *     if (处于按下态 且 长按未触发) {
 *         计算按下持续时间 = 当前时间 - 按下时间
 *         if (持续时间 >= 长按阈值) {
 *             标记长按已触发
 *             清除待处理短按（避免之后误触发双击）
 *             返回 长按事件
 *         }
 *     }
 *
 *     返回 无事件
 * }
 */
hal_event_t hal_input_dc_process(hal_input_dc_state_t *st,
                                  bool was_pressed,
                                  bool was_released,
                                  uint32_t now_ms)
{
    if (!st) return HAL_EVENT_NONE;

    /* 第一步：处理按下边沿（P1-1）
     * 必须在超时检查之前处理，否则窗口超时的同一帧出现新按下时，
     * 按下事件会被丢弃，导致用户感觉按键丢失。 */
    if (was_pressed) {
        /* 如果在窗口期内再次按下，进入双击序列 */
        if (st->pending_short_press &&
            (now_ms - st->last_release_ms) <= DOUBLE_CLICK_WINDOW_MS) {
            st->in_double_click_sequence = true;
            st->pending_short_press = false;
        }
        st->pressed = true;
        st->press_time = now_ms;
        st->long_fired = false;
        st->press_duration_ms = 0;
    }

    /* 第三步：处理释放边沿 */
    if (was_released) {
        st->pressed = false;
        st->press_duration_ms = 0;

        if (!st->long_fired) {
            if (st->in_double_click_sequence) {
                /* 双击序列中的第二次释放 → 双击 */
                st->in_double_click_sequence = false;
                st->last_release_ms = 0;
                return HAL_EVENT_DOUBLE_CLICK;
            }
            /* 第一次释放，记录时间，等待窗口期 */
            st->pending_short_press = true;
            st->last_release_ms = now_ms;
        } else {
            /* 长按后释放，重置所有状态 */
            st->long_fired = false;
            st->pending_short_press = false;
            st->in_double_click_sequence = false;
            st->last_release_ms = 0;
        }
    }

    /* 第三步：检查是否有超时等待的短按 */
    if (st->pending_short_press &&
        (now_ms - st->last_release_ms) > DOUBLE_CLICK_WINDOW_MS) {
        st->pending_short_press = false;
        st->last_release_ms = 0;
        return HAL_EVENT_SHORT_PRESS;
    }

    /* 第四步：检测长按 */
    if (st->pressed && !st->long_fired) {
        st->press_duration_ms = now_ms - st->press_time;
        if (st->press_duration_ms >= LONG_PRESS_DURATION_MS) {
            st->long_fired = true;
            /* 长按触发时清除双击序列和待处理短按 */
            st->pending_short_press = false;
            st->in_double_click_sequence = false;
            st->last_release_ms = 0;
            return HAL_EVENT_LONG_PRESS;
        }
    }

    return HAL_EVENT_NONE;
}

/* ═══ 简单状态机（无双击检测）═══ */

/**
 * @brief  简单按键事件检测（无双击检测，即时响应）
 * @param  st           状态机指针
 * @param  was_pressed  本帧是否检测到按下边沿
 * @param  was_released 本帧是否检测到释放边沿
 * @param  now_ms       当前时间戳（毫秒）
 * @return 事件类型；无事件时返回 HAL_EVENT_NONE
 * @note   时序规则：
 *         1. 按下后释放 → 立即返回 SHORT_PRESS（无窗口期延迟）
 *         2. 按下持续超过 LONG_PRESS_DURATION_MS → 返回 LONG_PRESS（仅一次）
 *         3. 长按后释放 → 重置状态，不返回额外事件
 *         4. 不检测双击
 *
 * 中文伪代码拆解：
 *
 * 函数 简单按键处理(状态, 按下边沿, 释放边沿, 当前时间) {
 *     // 第一步：按下边沿 → 记录状态
 *     if (按下边沿) {
 *         标记为按下态
 *         记录按下时间
 *         重置长按已触发标记
 *     }
 *
 *     // 第二步：释放边沿 → 立即判定
 *     if (释放边沿) {
 *         标记为释放态
 *         if (长按未触发) {
 *             重置所有后缀标志
 *             返回 短按事件
 *         }
 *         重置所有后缀标志
 *     }
 *
 *     // 第三步：持续按下中检测长按
 *     if (处于按下态 且 长按未触发) {
 *         计算持续时间 = 当前时间 - 按下时间
 *         if (持续时间 >= 长按阈值) {
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
                                      uint32_t now_ms)
{
    if (!st) return HAL_EVENT_NONE;

    /* 第一步：按下边沿 */
    if (was_pressed) {
        st->pressed = true;
        st->press_time = now_ms;
        st->long_fired = false;
        st->press_duration_ms = 0;
    }

    /* 第二步：释放边沿 → 立即返回短按 */
    if (was_released) {
        st->pressed = false;
        st->press_duration_ms = 0;
        if (!st->long_fired) {
            st->last_release_ms = 0;
            st->pending_short_press = false;
            st->in_double_click_sequence = false;
            return HAL_EVENT_SHORT_PRESS;
        }
        /* 长按后释放：重置状态，不返回额外事件 */
        st->long_fired = false;
        st->last_release_ms = 0;
        st->pending_short_press = false;
        st->in_double_click_sequence = false;
    }

    /* 第三步：持续按下中检测长按 */
    if (st->pressed && !st->long_fired) {
        st->press_duration_ms = now_ms - st->press_time;
        if (st->press_duration_ms >= LONG_PRESS_DURATION_MS) {
            st->long_fired = true;
            return HAL_EVENT_LONG_PRESS;
        }
    }

    return HAL_EVENT_NONE;
}
