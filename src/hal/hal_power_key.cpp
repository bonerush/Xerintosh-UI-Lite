/**
 * @file   hal_power_key.cpp
 * @brief  HAL 电源键驱动实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：通过测试注入模拟按下/释放，状态机驱动事件
 *          - 硬件环境时：轮询 AXP192 寄存器检测电源键事件
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_power_key.h"

/* ═══ 内部状态结构 ═══ */

typedef enum {
    PWR_STATE_IDLE,          /* 空闲 */
    PWR_STATE_PRESSED,       /* 按下中（未达到长按阈值） */
    PWR_STATE_LONG_FIRED,    /* 长按已触发，等待 HOLD */
    PWR_STATE_HOLD_FIRED     /* HOLD 已触发，等待下次重复或释放 */
} pwr_state_t;

struct pwr_key_state {
    pwr_state_t state;           /* 当前状态 */
    bool        pressed;         /* 当前是否按下 */
    uint32_t    press_time;      /* 按下起始时间戳 */
    uint32_t    last_event_time; /* 上次事件触发时间戳（用于 HOLD 重复） */
    hal_pwr_key_event_t pending; /* 待消费的事件 */
};

static struct pwr_key_state g_pwr;

/* ═══ 内部状态机 ═══ */

/**
 * @brief  处理电源键状态机
 * @param  now_ms  当前时间戳（毫秒）
 * @return 产生的事件（HAL_PWR_KEY_NONE 表示无新事件）
 *
 * 中文伪代码拆解：
 *
 * 函数 电源键状态机(当前时间) {
 *     if (当前按下) {
 *         计算按住时长 = 当前时间 - 按下时间
 *         if (状态 == 按下中) {
 *             if (按住时长 >= 1500ms) {
 *                 状态 = 长按已触发
 *                 记录事件时间
 *                 返回 长按事件
 *             }
 *         } else if (状态 >= 长按已触发) {
 *             if (距离上次事件 >= 500ms) {
 *                 状态 = 持续按住已触发
 *                 记录事件时间
 *                 返回 持续按住事件
 *             }
 *         }
 *     } else if (状态 == 按下中) {
 *         // 短按释放
 *         状态 = 空闲
 *         返回 短按事件
 *     } else if (状态 != 空闲) {
 *         // 长按/HOLD 后释放，不产生额外事件
 *         状态 = 空闲
 *     }
 *     返回 无事件
 * }
 */
static hal_pwr_key_event_t pwr_key_process(uint32_t now_ms)
{
    struct pwr_key_state *s = &g_pwr;

    if (s->pressed) {
        uint32_t duration = now_ms - s->press_time;

        if (s->state == PWR_STATE_PRESSED) {
            if (duration >= PWR_KEY_LONG_PRESS_MS) {
                s->state = PWR_STATE_LONG_FIRED;
                s->last_event_time = now_ms;
                return HAL_PWR_KEY_LONG_PRESS;
            }
        } else {
            /* LONG_FIRED 或 HOLD_FIRED：检查 HOLD 重复 */
            if ((now_ms - s->last_event_time) >= PWR_KEY_HOLD_REPEAT_MS) {
                s->state = PWR_STATE_HOLD_FIRED;
                s->last_event_time = now_ms;
                return HAL_PWR_KEY_HOLD;
            }
        }
    } else {
        /* 未按下：检查释放边沿 */
        if (s->state == PWR_STATE_PRESSED) {
            s->state = PWR_STATE_IDLE;
            return HAL_PWR_KEY_SHORT_PRESS;
        }
        if (s->state != PWR_STATE_IDLE) {
            s->state = PWR_STATE_IDLE;
        }
    }

    return HAL_PWR_KEY_NONE;
}

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：桩实现 ═══ */

/**
 * @brief 获取电源键事件（从内部状态机提取）
 */
hal_pwr_key_event_t hal_power_key_get_event(void)
{
    if (g_pwr.pending != HAL_PWR_KEY_NONE) {
        hal_pwr_key_event_t ev = g_pwr.pending;
        g_pwr.pending = HAL_PWR_KEY_NONE;
        return ev;
    }
    return HAL_PWR_KEY_NONE;
}

/**
 * @brief 获取当前按住时长
 */
uint32_t hal_power_key_get_hold_duration_ms(void)
{
    if (!g_pwr.pressed) return 0;
    /* 使用 last_event_time 近似；测试中通过 inject 提供精确时间 */
    return 0;
}

/**
 * @brief 检查电源键是否按下
 */
bool hal_power_key_is_pressed(void)
{
    return g_pwr.pressed;
}

/**
 * @brief  注入电源键按下/释放状态
 * @param  pressed true=按下，false=释放
 * @param  now_ms  当前时间戳（毫秒）
 * @note   测试代码调用此函数模拟按键，
 *         内部运行状态机并将产生的事件存入 pending
 */
void hal_power_key_test_inject(bool pressed, uint32_t now_ms)
{
    bool was_pressed = g_pwr.pressed;
    g_pwr.pressed = pressed;

    /* 按下边沿：记录时间，进入 PRESSED 状态 */
    if (pressed && !was_pressed) {
        g_pwr.press_time = now_ms;
        g_pwr.state = PWR_STATE_PRESSED;
    }

    /* 运行状态机，产生事件 */
    hal_pwr_key_event_t ev = pwr_key_process(now_ms);
    if (ev != HAL_PWR_KEY_NONE) {
        g_pwr.pending = ev;
    }
}

/**
 * @brief 重置电源键状态机
 */
void hal_power_key_test_reset(void)
{
    g_pwr.state = PWR_STATE_IDLE;
    g_pwr.pressed = false;
    g_pwr.press_time = 0;
    g_pwr.last_event_time = 0;
    g_pwr.pending = HAL_PWR_KEY_NONE;
}

#else

/* ═══ 硬件环境：AXP192 轮询实现 ═══ */

#include <M5Unified.h>

/**
 * @brief AXP192 PEK（Power Enable Key）返回值
 * @note  寄存器 0x46 (IRQ status 3)：
 *        Bit 0 (0x01): 长按中断标志（按下 ≥ 1.5s）
 *        Bit 1 (0x02): 短按中断标志（按下 < 1.5s）
 *        读取后自动清除。
 *
 * 中文伪代码拆解：
 *
 * 函数 初始化电源键() {
 *     重置所有状态
 *     // AXP192 PEK 中断在 M5.begin() 时已默认启用
 *     // 清除可能残留的中断标志
 *     读取并丢弃 getPekPress()
 * }
 *
 * 函数 获取电源键事件() {
 *     读取 pek = getPekPress()
 *
 *     if (pek != 0) {
 *         // 检测到按下边沿
 *         if (pek == 2) {
 *             // 短按事件（按下了但 < 1.5s 就触发了中断）
 *             标记按下态, 记录按下时间
 *             返回 HAL_PWR_KEY_NONE（等待释放确认）
 *         } else {
 *             // 长按事件（按下 ≥ 1.5s）
 *             标记长按态, 记录按下时间
 *             返回 HAL_PWR_KEY_LONG_PRESS
 *         }
 *     }
 *
 *     if (当前按下) {
 *         计算按住时长
 *         if (短按中 且 按住时长 >= 1500ms) {
 *             // 超过长按阈值仍未释放 → 升级为长按
 *             转换为长按态
 *             返回 HAL_PWR_KEY_LONG_PRESS
 *         }
 *         if (长按态 且 距上次事件 >= 500ms) {
 *             返回 HAL_PWR_KEY_HOLD
 *         }
 *         if (短按中 且 超过最大等待时间 2000ms) {
 *             // 安全超时：假设已释放
 *             标记释放
 *             返回 HAL_PWR_KEY_SHORT_PRESS
 *         }
 *         if (长按态 且 超过最大等待时间 10000ms) {
 *             // 安全超时：长按超过 10s 假设已释放
 *             标记释放
 *         }
 *     }
 *
 *     返回 HAL_PWR_KEY_NONE
 * }
 */

/* 安全超时：AXP192 无法检测释放，超过此时间假设按键已释放 */
#define PWR_KEY_SHORT_TIMEOUT_MS  2000  /* 短按最大等待（含消抖余量） */
#define PWR_KEY_LONG_TIMEOUT_MS  10000  /* 长按最大等待 */

/**
 * @brief  获取电源键事件（非阻塞，每帧调用）
 * @details 轮询 AXP192 的 getPekPress() 方法（读取寄存器 0x46），
 *          结合时间推算检测短按、长按和持续按住事件。
 */
hal_pwr_key_event_t hal_power_key_get_event(void)
{
    /* 优先返回已缓存的事件 */
    if (g_pwr.pending != HAL_PWR_KEY_NONE) {
        hal_pwr_key_event_t ev = g_pwr.pending;
        g_pwr.pending = HAL_PWR_KEY_NONE;
        return ev;
    }

    uint32_t now_ms = millis();
    /* 使用 M5.Power.getKeyState() 而不是 M5.Power.Axp192.getPekPress()
     * 返回值：0=none / 1=long pressed / 2=short clicked / 3=both */
    uint8_t pek = M5.Power.getKeyState();

    /* 检测按下边沿：getKeyState() 非零表示新的按键事件 */
    if (pek != 0) {
        g_pwr.pressed = true;
        g_pwr.press_time = now_ms;

        if (pek == 0x02) {
            /* 短按事件（按下 < 1.5s 时触发）
             * 但此时键可能仍被按住，等待释放或超时升级为长按 */
            g_pwr.state = PWR_STATE_PRESSED;
        } else {
            /* 长按事件（按下 ≥ 1.5s 时触发）或 both
             * 直接返回 LONG_PRESS */
            g_pwr.state = PWR_STATE_LONG_FIRED;
            g_pwr.last_event_time = now_ms;
            return HAL_PWR_KEY_LONG_PRESS;
        }
    }

    /* 运行状态机处理持续按住和释放检测 */
    if (g_pwr.pressed) {
        uint32_t duration = now_ms - g_pwr.press_time;

        if (g_pwr.state == PWR_STATE_PRESSED) {
            /* 等待短按释放或超时升级 */
            if (duration >= PWR_KEY_LONG_PRESS_MS) {
                /* 按住超过 1.5s 未收到新的 PEK 事件 → 升级为长按 */
                g_pwr.state = PWR_STATE_LONG_FIRED;
                g_pwr.last_event_time = now_ms;
                return HAL_PWR_KEY_LONG_PRESS;
            }
            if (duration >= PWR_KEY_SHORT_TIMEOUT_MS) {
                /* 安全超时：短按后超过 2s 无释放信号 → 假设已释放 */
                g_pwr.pressed = false;
                g_pwr.state = PWR_STATE_IDLE;
                return HAL_PWR_KEY_SHORT_PRESS;
            }
        } else {
            /* LONG_FIRED 或 HOLD_FIRED：检查 HOLD 重复 */
            if ((now_ms - g_pwr.last_event_time) >= PWR_KEY_HOLD_REPEAT_MS) {
                g_pwr.state = PWR_STATE_HOLD_FIRED;
                g_pwr.last_event_time = now_ms;
                return HAL_PWR_KEY_HOLD;
            }
            /* 长按安全超时 */
            if (duration >= PWR_KEY_LONG_TIMEOUT_MS) {
                g_pwr.pressed = false;
                g_pwr.state = PWR_STATE_IDLE;
            }
        }
    }

    return HAL_PWR_KEY_NONE;
}

/**
 * @brief 获取当前按住时长
 */
uint32_t hal_power_key_get_hold_duration_ms(void)
{
    if (!g_pwr.pressed) return 0;
    return millis() - g_pwr.press_time;
}

/**
 * @brief 检查电源键是否按下
 */
bool hal_power_key_is_pressed(void)
{
    return g_pwr.pressed;
}

#endif
