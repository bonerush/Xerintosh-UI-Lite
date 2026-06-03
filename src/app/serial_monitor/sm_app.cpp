/**
 * @file   sm_app.cpp
 * @brief  串口监视器 App 生命周期与后台读取
 * @details 实现串口监视器的初始化、主循环、退出及后台数据读取。
 *          支持有线串口（SER）和蓝牙串口（BLE）双数据源。
 *
 * @copyright Copyright (c) 2026
 */

#include "sm_app.h"
#include "serial_monitor.h"
#include "sm_ui.h"
#include "app/settings/settings.h"
#include "app/serial_input/serial_input.h"
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

/* ═══ 状态变量 ═══ */

bool        sm_running = false;
sm_source_t sm_source = SM_SOURCE_SER;
uint8_t     sm_selected = 0;
bool        sm_bt_connected = false;
static uint32_t    sm_blink_tick = 0;
static bool        sm_blink_on = false;
sm_buffer_t sm_buffer;

/* 动画状态 */
float       sm_entry_offset = 0.0f;
static float       sm_btn_alpha_0 = 1.0f;
float       sm_btn_alpha_1 = 0.0f;

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>
static char        sm_rx_buf[SM_TERM_LINE_LEN];
static uint8_t     sm_rx_len = 0;
static bool        s_prev_landscape = true; /* 保存进入前的屏幕方向 */

/* BT RX 行组装缓冲区（迁移自 ble_serial.cpp） */
static char     sm_bt_rx_buf[SM_TERM_LINE_LEN];
static uint8_t  sm_bt_rx_len = 0;
static uint32_t sm_bt_rx_count = 0;  /* BT RX 字节计数 */
static bool     sm_prev_bt_connected = false; /* 连接状态变化检测 */
#endif

/* ═══ 常量 ═══ */

#define SM_BLINK_PERIOD 500  /* 反色闪烁周期（毫秒） */

#ifndef NATIVE_TEST
/**
 * @brief BT RX 回调（在 UI 任务 bt_uart_drain_rx_queue 上下文中执行）
 * @note  行组装逻辑：逐字节累积，遇 \n/\r 时写入 sm_buffer。
 *        仅在监视器运行中（sm_running）时处理数据，尊重 RUN/STOP 状态。
 */
static void sm_on_bt_rx(const uint8_t *data, uint16_t len)
{
    if (!sm_running) return;

    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];
        sm_bt_rx_count++;
        if (c == '\n' || c == '\r') {
            if (sm_bt_rx_len > 0) {
                sm_bt_rx_buf[sm_bt_rx_len] = '\0';
                sm_buffer_add_line(&sm_buffer, sm_bt_rx_buf, true);
                sm_bt_rx_len = 0;
            }
        } else if (sm_bt_rx_len < SM_TERM_LINE_LEN - 1) {
            sm_bt_rx_buf[sm_bt_rx_len++] = c;
        }
    }
}

/**
 * @brief BT 连接状态变化回调
 */
static void sm_on_bt_connect(bool connected)
{
    sm_bt_connected = connected;
}
#endif

/**
 * @brief 初始化串口监视器 App
 * @note  重置所有状态、清空缓冲区、初始化动画变量
 */
void serial_monitor_init(void *ud)
{
    (void)ud;
    sm_running = false;
    sm_source = SM_SOURCE_SER;
    sm_selected = 0;
    sm_bt_connected = false;
    sm_blink_tick = hal_get_ticks();
    sm_blink_on = false;
    sm_buffer_init(&sm_buffer);

    /* 初始化动画状态 */
    sm_entry_offset = (float)SCREEN_HEIGHT;
    sm_btn_alpha_0 = 100.0f;
    sm_btn_alpha_1 = 0.0f;

#ifndef NATIVE_TEST
    sm_rx_len = 0;
    sm_bt_rx_len = 0;
    sm_bt_rx_count = 0;
    sm_prev_bt_connected = false;
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
    hal_input_set_double_click_enabled(true);

    /* 注册 BT 回调（BLE 模式下使用） */
    bt_uart_set_rx_callback(sm_on_bt_rx);
    bt_uart_set_connect_callback(sm_on_bt_connect);
#endif
}

/**
 * @brief 串口监视器主循环（每帧调用）
 * @note  处理输入事件、更新动画、绘制信息栏和终端
 */
void serial_monitor_loop(void *ud)
{
    (void)ud;
    /* 第一步：读取按键事件 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* 第二步：短按切换选择器 */
    if (event_a == HAL_EVENT_SHORT_PRESS ||
        event_b == HAL_EVENT_SHORT_PRESS) {
        sm_selected = !sm_selected;
    }

    /* 第三步：长按确认按钮切换状态 */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        if (sm_selected == 0) {
            sm_running = !sm_running;
        } else {
            /* 切换数据源 SER ↔ BLE */
            if (sm_source == SM_SOURCE_SER) {
                sm_source = SM_SOURCE_BLE;
                sm_bt_connected = bt_uart_is_connected();
            } else {
                sm_source = SM_SOURCE_SER;
            }
            /* 切换源时清空缓冲区，避免混淆 */
            sm_buffer_clear(&sm_buffer);
        }
    }

    /* 第四步：长按返回按钮退出 App */
    if (ui_user_item_try_exit(event_b)) return;

    /* 第五步：双击滚动终端 */
    if (event_a == HAL_EVENT_DOUBLE_CLICK) {
        if (sm_buffer.scroll < sm_buffer.count - 1) {
            sm_buffer.scroll++;
        }
    }
    if (event_b == HAL_EVENT_DOUBLE_CLICK) {
        if (sm_buffer.scroll > 0) {
            sm_buffer.scroll--;
        }
    }

    /* ── 动画更新 ── */

    /* 入场滑入动画 */
    xerintosh_animation(&sm_entry_offset, 0.0f, ANIM_SPEED_EXIT);

    /* 按钮选中平滑过渡（[0,100] 范围确保 xerintosh_animation 差值 > 1.0，触发逐帧动画） */
    float trg0 = (sm_selected == 0) ? 100.0f : 0.0f;
    float trg1 = (sm_selected == 1) ? 100.0f : 0.0f;
    xerintosh_animation(&sm_btn_alpha_0, trg0, ANIM_SPEED_SELECTOR);
    xerintosh_animation(&sm_btn_alpha_1, trg1, ANIM_SPEED_SELECTOR);

    /* 保留闪烁相位更新（向后兼容，供 draw_button 的阈值判断使用） */
    uint32_t now = hal_get_ticks();
    if (now - sm_blink_tick >= SM_BLINK_PERIOD) {
        sm_blink_tick = now;
        sm_blink_on = !sm_blink_on;
    }

#ifndef NATIVE_TEST
    /* BLE 模式下：消费 RX 队列 + 更新连接状态 + 弹窗提示
     * bt_uart_drain_rx_queue() 在 UI 任务上下文中调用回调，
     * 确保 sm_buffer 写入与 serial_monitor_draw() 读取在同一任务，消除跨任务竞争。 */
    if (sm_source == SM_SOURCE_BLE) {
        if (sm_running) {
            bt_uart_drain_rx_queue();
        }
        bool prev = sm_bt_connected;
        sm_bt_connected = bt_uart_is_connected();
        if (sm_bt_connected != prev) {
            xerintosh_push_pop_up(
                sm_bt_connected ? "Connected" : "Disconnected", 1500);
            sm_prev_bt_connected = sm_bt_connected;
        }
    }
#endif

    /* 绘制界面 */
    serial_monitor_draw();
}

/**
 * @brief 退出串口监视器 App
 * @note  清空缓冲区、注销 BT 回调、恢复屏幕方向
 */
void serial_monitor_exit(void *ud)
{
    (void)ud;
    /* 始终清空缓冲区 */
    sm_buffer_clear(&sm_buffer);

#ifndef NATIVE_TEST
    /* 注销 BT 回调 */
    bt_uart_set_rx_callback(NULL);
    bt_uart_set_connect_callback(NULL);

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

/**
 * @brief 后台串口数据读取
 * @note  供 main.cpp 的 loop() 每帧调用。
 *        仅在 SER 模式下读取硬件 UART 数据。
 *        BLE 模式下数据由 bt_uart_poll → sm_on_bt_rx 回调写入。
 *        若 serial_input 处于 WAITING 状态，暂停读取避免竞争。
 */
void serial_monitor_update(void)
{
    if (!sm_running) return;

    /* BLE 模式：数据由 bt_uart_poll → sm_on_bt_rx 回调写入，无需在此处主动读取 */
    if (sm_source != SM_SOURCE_SER) return;

    /*
     * 当 serial_input 正在等待密码/配对码时，不消费串口字符。
     * 将字符留在硬件 Serial 缓冲区中，由 serial_poll() 直接消费。
     */
    if (serial_input_is_waiting()) {
        return;
    }

#ifndef NATIVE_TEST
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;

        if (c == '\n' || c == '\r') {
            if (sm_rx_len > 0) {
                sm_rx_buf[sm_rx_len] = '\0';
                sm_buffer_add_line(&sm_buffer, sm_rx_buf, true);
                sm_rx_len = 0;
            }
        } else if (sm_rx_len < SM_TERM_LINE_LEN - 1) {
            sm_rx_buf[sm_rx_len++] = (char)c;
        }
    }
#endif
}

/**
 * @brief 查询串口监视器是否正在活跃
 * @note  仅在 SER 模式运行时阻止 dev_ttyS0 消费 Serial 数据。
 *        BLE 模式下不消费硬件 UART，Shell 和 dev_ttyS0 应正常工作。
 */
extern "C" bool serial_monitor_is_active(void)
{
    return sm_running && sm_source == SM_SOURCE_SER;
}
