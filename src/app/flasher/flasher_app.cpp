/**
 * @file   flasher_app.cpp
 * @brief  烧录器 user_item App 生命周期
 * @details 实现固件烧录器的初始化、主循环、退出及蓝牙串口数据接收。
 *          基于已实现的 GPIO/Protocol/UI 模块，提供用户可交互的烧录界面。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher.h"
#include "flasher_gpio.h"
#include "flasher_protocol.h"
#include "flasher_ui.h"
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"
#include "app/settings/settings.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_selector.h"
#include "ui/ui_item.h"

/* ═══ 状态变量 ═══ */

static flasher_ui_state_t    s_ui;
static flasher_session_t     s_session;
static bool                  s_running = false;
static bool                  s_prev_landscape = true;
static float                 s_entry_offset = 0.0f;
static bool                  s_bt_lazy_inited = false;

#define FW_BUF_SIZE 2048
static uint8_t s_fw_buf[FW_BUF_SIZE];
static int     s_fw_len = 0;

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>

/**
 * @brief BT RX 回调（在 UI 任务 bt_uart_drain_rx_queue 上下文中执行）
 * @note  将接收到的固件数据累积到缓冲区，供协议状态机消费。
 *        仅在烧录器运行中（s_running）时处理数据。
 */
static void flasher_on_bt_rx(const uint8_t *data, uint16_t len)
{
    if (!s_running) return;
    for (uint16_t i = 0; i < len && s_fw_len < FW_BUF_SIZE; i++) {
        s_fw_buf[s_fw_len++] = data[i];
    }
}
#endif

/**
 * @brief 初始化烧录器 App
 * @note  重置所有状态、初始化 UI 与会话、设置横屏方向并注册蓝牙回调。
 */
void flasher_init(void *ud)
{
    (void)ud;
    s_running = false;
    s_fw_len = 0;
    s_bt_lazy_inited = false;
    s_entry_offset = (float)SCREEN_HEIGHT;

    flasher_ui_init(&s_ui);
    flasher_session_init(&s_session, 0x10000, 0);

#ifndef NATIVE_TEST
    s_prev_landscape = g_is_landscape;
    if (!g_is_landscape) {
        /* 从竖屏菜单进入时临时切换到横屏 */
        g_is_landscape = true;
        g_screen_rotation_level = ORIENTATION_LANDSCAPE;
        M5.Display.setRotation(1);
        g_screen_width = M5.Display.width();
        g_screen_height = M5.Display.height();
        hal_display_init();
    }
    hal_input_reset_events();
    hal_input_set_double_click_enabled(false);

    /* 注册 BT 回调 */
    bt_uart_set_rx_callback(flasher_on_bt_rx);

    /* 懒加载 BT：如果 BT 未启用，按需初始化 */
    if (!bt_mgr_is_enabled()) {
        bt_mgr_enable();
        s_bt_lazy_inited = true;
    }
#endif
}

/**
 * @brief 烧录器主循环（每帧调用）
 * @note  处理按键事件、更新动画、绘制进度条 UI。
 *        长按 A 切换运行/停止状态，长按 B 退出 App。
 */
void flasher_loop(void *ud)
{
    (void)ud;
    /* 第一步：读取按键事件 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* 第二步：长按 B 退出 App */
    if (ui_user_item_try_exit(event_b)) return;

    /* 第三步：长按 A 切换运行状态 */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        s_running = !s_running;
        if (s_running) {
            flasher_ui_set_status(&s_ui, FLASHER_UI_LOADING);
            s_fw_len = 0;
            s_session.state = FLASHER_STATE_CONNECTING;
        } else {
            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
            s_session.state = FLASHER_STATE_FAILED;
        }
    }

    /* 第四步：入场滑入动画 */
    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

#ifndef NATIVE_TEST
    /* 第五步：运行中消费蓝牙 RX 队列 */
    if (s_running) {
        bt_uart_drain_rx_queue();
    }

    /* 第六步：模拟进度（完整协议状态机将在 Task 6 连线） */
    if (s_fw_len > 0 && s_ui.status == FLASHER_UI_LOADING) {
        int demo_pct = (s_fw_len * 100) / FW_BUF_SIZE;
        if (demo_pct > 99) demo_pct = 99;
        flasher_ui_set_progress(&s_ui, demo_pct);
    }
#endif

    /* 第七步：绘制界面 */
    flasher_ui_draw(&s_ui);
}

/**
 * @brief 退出烧录器 App
 * @note  停止运行、注销 BT 回调、恢复屏幕方向。
 */
void flasher_exit(void *ud)
{
    (void)ud;
    s_running = false;

#ifndef NATIVE_TEST
    /* 注销 BT 回调 */
    bt_uart_set_rx_callback(NULL);

    /* 如果 BT 是由烧录器懒加载的，退出时释放以归还内存给 WiFi */
    if (s_bt_lazy_inited && bt_mgr_is_enabled()) {
        bt_mgr_disable();
        s_bt_lazy_inited = false;
    }

    if (!s_prev_landscape) {
        /* 恢复之前的竖屏方向 */
        g_is_landscape = false;
        g_screen_rotation_level = ORIENTATION_PORTRAIT;
        M5.Display.setRotation(0);
        g_screen_width = M5.Display.width();
        g_screen_height = M5.Display.height();
        hal_display_init();
    }
    hal_input_set_double_click_enabled(false);
    hal_input_reset_events();
#endif
}
