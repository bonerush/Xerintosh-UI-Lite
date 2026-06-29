/**
 * @file   sm_app.cpp
 * @brief  串口监视器 App 生命周期与后台读取
 * @details 实现串口监视器的初始化、主循环、退出及后台数据读取。
 *          支持有线串口（SER）和蓝牙串口（BT）两种数据源。
 *          BT 模式通过 bt_uart_service 回调接收数据。
 *
 * @copyright Copyright (c) 2026
 */

#include "sm_app.h"
#include "serial_monitor.h"
#include "sm_ui.h"
#include "app/serial_input/serial_input.h"
#include "app/ui_service.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#ifndef NATIVE_TEST
#include "app/bluetooth/bt_uart_service.h"
#include "app/wifi/wifi_manager.h"
#include "app/app_state.h"
#include "esp_log.h"
#endif

/* 串口监视器调试开关：0=禁用每帧调试日志，1=启用 */
#define SM_DBG_ENABLED 0

/* ═══ 状态变量 ═══ */

bool        sm_running = false;
uint8_t     sm_selected = 0;
sm_source_t sm_source  = SM_SOURCE_SER;
sm_buffer_t sm_buffer;

static uint32_t    sm_blink_tick = 0;

/* 进入串口监视器时自动开启 BT 的状态跟踪 */
#ifndef NATIVE_TEST
static bool s_auto_bt_active = false;
static bool s_auto_wifi_was_on = false;
#endif

/* 动画状态 */
float       sm_entry_offset = 0.0f;
static float       sm_entry_vel    = 0.0f;
float       sm_btn_alpha_0  = 100.0f;
float       sm_btn_alpha_1  = 0.0f;
static float       sm_btn_vel_0    = 0.0f;
static float       sm_btn_vel_1    = 0.0f;

#ifndef NATIVE_TEST
#include "hal/hal_uart.h"
static char        sm_rx_buf[SM_TERM_LINE_LEN];
static uint8_t     sm_rx_len = 0;
#endif

/* ═══ 常量 ═══ */

#define SM_BLINK_PERIOD 500  /* 反色闪烁周期（毫秒） */

/**
 * @brief BT SPP RX 数据回调
 * @note  由 bt_uart_poll() 在 main_loop_task 上下文中调用。
 *        将接收到的字节追加到 sm_rx_buf，遇到换行时提交为一行。
 *        仅在 BT 模式下处理数据（SER 模式由 serial_monitor_update 处理）。
 */
#ifndef NATIVE_TEST
static void sm_on_bt_rx(const uint8_t *data, uint16_t len)
{
    if (sm_source != SM_SOURCE_BT) return;
    if (!sm_running) return;

    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];

        /* 回显：将收到的字符发送回 BT 终端（如同 Shell 对 UART 的回显） */
        if (c == '\n') {
            bt_uart_send_string("\r\n");
        } else {
            bt_uart_send((const uint8_t *)&c, 1);
        }

        if (c == '\n' || c == '\r') {
            if (sm_rx_len > 0) {
                sm_rx_buf[sm_rx_len] = '\0';
                sm_buffer_add_line(&sm_buffer, sm_rx_buf, true);
                sm_rx_len = 0;
            }
        } else if (sm_rx_len < SM_TERM_LINE_LEN - 1) {
            sm_rx_buf[sm_rx_len++] = c;
        }
    }

    /* 与普通串口（SER 模式）一致：等待 \n 才提交 */
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
    sm_selected = 0;
    sm_source  = SM_SOURCE_SER;
    sm_blink_tick = hal_get_ticks();
    sm_buffer_init(&sm_buffer);

    /* 初始化动画状态 */
    sm_entry_offset = (float)HAL_SCREEN_HEIGHT;
    sm_entry_vel    = 0.0f;
    sm_btn_alpha_0  = 100.0f;
    sm_btn_alpha_1  = 0.0f;
    sm_btn_vel_0    = 0.0f;
    sm_btn_vel_1    = 0.0f;

#ifndef NATIVE_TEST
    sm_rx_len = 0;
    /* 自动开启 BT、关闭 WiFi（如果未开启 BT） */
    if (!g_bt_on) {
        s_auto_wifi_was_on = wifi_mgr_is_enabled();
        if (s_auto_wifi_was_on) {
            wifi_mgr_disable();
            g_wifi_on = false;
        }
        if (bt_uart_service_init()) {
            g_bt_on = true;
            s_auto_bt_active = true;
        } else if (s_auto_wifi_was_on) {
            /* BT 初始化失败，恢复 WiFi */
            wifi_mgr_enable();
            g_wifi_on = true;
        }
    }
    /* 注册 BT RX 回调（必须在 bt_uart_service_init 之后，否则会被 init 中的 g_rx_cb=NULL 覆盖） */
    bt_uart_set_rx_callback(sm_on_bt_rx);
    ui_service_enter_landscape();
    ui_service_user_item_init();
    hal_input_set_double_click_enabled(true);
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

    /* 第三步：长按确认按钮切换 RUN/STOP 或数据源模式 */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        if (sm_selected == 0) {
            sm_running = !sm_running;
        } else {
            /* sm_selected == 1: 切换 SER/BT 数据源 */
            sm_source = (sm_source == SM_SOURCE_SER) ? SM_SOURCE_BT : SM_SOURCE_SER;
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
    xerintosh_animate_unified(&sm_entry_offset, &sm_entry_vel, 0.0f, ANIM_SPEED_EXIT);

    /* 按钮选中平滑过渡（[0,100] 范围确保动画差值 > 1.0，触发逐帧动画） */
    float trg0 = (sm_selected == 0) ? 100.0f : 0.0f;
    float trg1 = (sm_selected == 1) ? 100.0f : 0.0f;
    xerintosh_animate_unified(&sm_btn_alpha_0, &sm_btn_vel_0, trg0, ANIM_SPEED_SELECTOR);
    xerintosh_animate_unified(&sm_btn_alpha_1, &sm_btn_vel_1, trg1, ANIM_SPEED_SELECTOR);

    /* 保留闪烁相位更新（向后兼容，供 draw_button 的阈值判断使用） */
    uint32_t now = hal_get_ticks();
    if (now - sm_blink_tick >= SM_BLINK_PERIOD) {
        sm_blink_tick = now;
    }

    /* 绘制界面 */
    serial_monitor_draw();
}

/**
 * @brief 退出串口监视器 App
 * @note  清空缓冲区、恢复屏幕方向
 */
void serial_monitor_exit(void *ud)
{
    (void)ud;
    /* 始终清空缓冲区 */
    sm_buffer_clear(&sm_buffer);

#ifndef NATIVE_TEST
    /* 自动关闭 BT、恢复 WiFi */
    if (s_auto_bt_active) {
        bt_uart_service_deinit();
        g_bt_on = false;
        s_auto_bt_active = false;
        if (s_auto_wifi_was_on) {
            wifi_mgr_enable();
            g_wifi_on = true;
            s_auto_wifi_was_on = false;
        }
    }
    ui_service_exit_landscape();
    hal_input_set_double_click_enabled(false);
    ui_service_user_item_exit();
#endif
}

/**
 * @brief 后台串口数据读取
 * @note  供 main.cpp 的 loop() 每帧调用。
 *        SER 模式：仅在运行中时读取硬件 UART 数据并缓存到环形缓冲区。
 *        BT 模式：数据由 bt_uart_service 回调直接写入，此处不操作。
 *        若 serial_input 处于 WAITING 状态，暂停读取避免竞争。
 */
void serial_monitor_update(void)
{
    if (!sm_running) return;

    /* BT 模式：数据由 bt_uart_poll → sm_on_bt_rx 处理 */
    if (sm_source == SM_SOURCE_BT) return;

    /*
     * 当 serial_input 正在等待密码/配对码时，不消费串口字符。
     * 将字符留在硬件 Serial 缓冲区中，由 serial_poll() 直接消费。
     */
    if (serial_input_is_waiting()) {
        return;
    }

#ifndef NATIVE_TEST
    serial_uart_lock();
    uint8_t byte;
    while (hal_uart0_read(&byte, 1) > 0) {
        char c = (char)byte;

        /* 回显到 UART（如同 Shell 的回显） */
        if (c == '\n') {
            hal_uart0_write((const uint8_t *)"\r\n", 2);
        } else {
            hal_uart0_write(&byte, 1);
        }

        if (c == '\n' || c == '\r') {
            if (sm_rx_len > 0) {
                sm_rx_buf[sm_rx_len] = '\0';
                sm_buffer_add_line(&sm_buffer, sm_rx_buf, true);
                sm_rx_len = 0;
            }
        } else if (sm_rx_len < SM_TERM_LINE_LEN - 1) {
            sm_rx_buf[sm_rx_len++] = c;
        }
    }
    serial_uart_unlock();
#endif
}

/**
 * @brief 查询串口监视器是否正在活跃（仅 SER 模式）
 * @note  仅在 SER 模式运行中时阻止 dev_ttyS0 消费 UART 数据。
 *        BT 模式下 UART 数据仍由 dev_ttyS0/Shell 正常处理。
 */
extern "C" bool serial_monitor_is_active(void)
{
    return sm_running && (sm_source == SM_SOURCE_SER);
}
