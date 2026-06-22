/**
 * @file   power_key_popup.c
 * @brief  电源键弹窗模块实现
 * @details M5StickC 的电源键仅连接到 AXP192，无法通过软件检测。
 *          使用 A+B 双键同时长按 3 秒作为替代关机方案。
 *          检测到双键时隔离正常按钮事件，显示倒计时弹窗，
 *          超时后通过 AXP192 硬件断电实现真正关机。
 *
 * @copyright Copyright (c) 2026
 */

#include "power_key_popup.h"
#include "shutdown_screen.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_item.h"
#include <stdio.h>

/* C++ 包装函数（定义在 hal_power_off.cpp 中） */
extern void hal_power_off_hw(void);

#define SHUTDOWN_HOLD_MS 3000  /* 双键长按关机阈值（毫秒） */

/* ═══ 内部状态 ═══ */

static bool g_dual_active = false;             /* 双键检测模式激活 */
static bool g_dual_shutdown_triggered = false;  /* 已触发关机，防止重复 */
static uint32_t g_dual_start_ms = 0;           /* 双键同时按下的起始时间 */
static uint32_t g_dual_cooldown_end_ms = 0;    /* 双键松手后的冷却期截止时间 */

#define DUAL_RELEASE_COOLDOWN_MS 300           /* 松手后隔离输入的冷却时长（毫秒） */

/* ═══ 公共 API ═══ */

void power_key_popup_init(void)
{
    g_dual_active = false;
    g_dual_shutdown_triggered = false;
    g_dual_start_ms = 0;
    g_dual_cooldown_end_ms = 0;
}

/**
 * @brief 检测 A+B 双键长按，显示倒计时弹窗并触发关机
 * @note  自行管理计时，不依赖 hal_input 的 press_duration_ms。
 *        检测到双键按下时设置 g_dual_active，阻止正常按钮事件处理。
 */
void power_key_popup_update(void)
{
    bool a_pressed = hal_input_is_pressed(HAL_BTN_A);
    bool b_pressed = hal_input_is_pressed(HAL_BTN_B);

    if (a_pressed && b_pressed)
    {
        g_dual_cooldown_end_ms = 0;  /* 重新进入双键模式时清除冷却 */

        if (!g_dual_active)
        {
            /* 首次检测到双键同时按下，记录起始时间 */
            g_dual_active = true;
            g_dual_start_ms = hal_get_ticks();
        }

        uint32_t dur = hal_get_ticks() - g_dual_start_ms;
        uint32_t remaining_ms = (dur >= SHUTDOWN_HOLD_MS) ? 0 : (SHUTDOWN_HOLD_MS - dur);

        if (dur >= SHUTDOWN_HOLD_MS && !g_dual_shutdown_triggered)
        {
            g_dual_shutdown_triggered = true;
            xerintosh_hide_pop_up();
            /* 先显示关机画面，再硬件断电 */
            shutdown_screen_show();
            hal_power_off_hw();
        }
        else if (!g_dual_shutdown_triggered)
        {
            static char msg[48];
            uint32_t sec = remaining_ms / 1000;
            uint32_t tenth = (remaining_ms % 1000) / 100;
            snprintf(msg, sizeof(msg), "长按AB键关机,还需%lu.%lus",
                     (unsigned long)sec, (unsigned long)tenth);
            xerintosh_push_pop_up(msg, SHUTDOWN_HOLD_MS);
        }
    }
    else
    {
        /* 仅在刚退出双键模式时触发一次动画退出，而非每帧 dismiss，
           否则会错误地杀死其他模块（如 G36、强制解除）推送的弹窗 */
        if (g_dual_active)
        {
            g_dual_cooldown_end_ms = hal_get_ticks() + DUAL_RELEASE_COOLDOWN_MS;
            hal_input_reset_events();
            xerintosh_dismiss_pop_up();
        }
        g_dual_active = false;
        g_dual_shutdown_triggered = false;
        g_dual_start_ms = 0;
    }
}

bool power_key_popup_is_visible(void)
{
    return g_dual_active;
}

bool power_key_popup_is_dual_active(void)
{
    if (g_dual_active) return true;
    if (g_dual_cooldown_end_ms != 0 && hal_get_ticks() < g_dual_cooldown_end_ms) return true;
    return false;
}
