/**
 * @file   bt_uart_service.cpp
 * @brief  蓝牙串口服务实现（Classic BT SPP）
 * @details 双实现架构：
 *          - NATIVE_TEST 时：提供可测试的桩实现
 *          - 硬件环境时：包装 hal_bt 层，管理连接状态和回调分发
 *
 *          线程安全策略：
 *          - hal_bt 的 SPP 回调运行在 Bluedroid 内部任务上下文，
 *            数据通过原子环形缓冲区安全传递
 *          - bt_uart_poll() 运行在 Xeros 主任务上下文，读取环形缓冲区
 *            并通过回调分发给应用程序
 *
 * @copyright Copyright (c) 2026
 */

#include "app/bluetooth/bt_uart_service.h"
#include <string.h>

/*
 * 编译路径选择：
 *   NATIVE_TEST              → 桩实现（用于单元测试）
 *   CONFIG_BT_ENABLED 已定义 → 硬件实现（包装 hal_bt）
 *   其他                      → 空桩（BT 未编译）
 */

#if defined(NATIVE_TEST)

#include <stddef.h>

/* ═══ NATIVE_TEST 桩实现 ═══ */

static bool            g_bt_connected = false;
static bt_uart_rx_callback_t     g_rx_cb    = NULL;
static bt_uart_connect_callback_t g_conn_cb = NULL;

bool bt_uart_service_init(void)
{
    g_bt_connected = false;
    g_rx_cb        = NULL;
    g_conn_cb      = NULL;
    return true;
}

void bt_uart_service_deinit(void)
{
    g_bt_connected = false;
    g_rx_cb        = NULL;
    g_conn_cb      = NULL;
}

uint16_t bt_uart_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return 0;
    return g_bt_connected ? len : 0;
}

uint16_t bt_uart_send_string(const char *str)
{
    if (!str) return 0;
    return bt_uart_send((const uint8_t *)str, (uint16_t)strlen(str));
}

void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback) { g_rx_cb = callback; }
void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback) { g_conn_cb = callback; }
bool bt_uart_is_connected(void) { return g_bt_connected; }
uint16_t bt_uart_get_tx_buffer_usage(void) { return 0; }
uint16_t bt_uart_get_rx_buffer_usage(void) { return 0; }
void bt_uart_poll(void) {}

/* ═══ 测试辅助函数（仅 NATIVE_TEST，extern "C" 供测试链接） ═══ */

extern "C" {

void bt_uart_test_simulate_connect(bool connected)
{
    g_bt_connected = connected;
    if (g_conn_cb) g_conn_cb(connected);
}

void bt_uart_test_simulate_rx(const uint8_t *data, uint16_t len)
{
    if (g_rx_cb) g_rx_cb(data, len);
}

} /* extern "C" */

#elif defined(CONFIG_BT_ENABLED)
/* ═══ 硬件环境：CONFIG_BT_ENABLED=y ═══ */

#include "hal/hal_bt.h"

static bool            g_bt_initialized = false;
static bool            g_bt_prev_connected = false;
static bt_uart_rx_callback_t     g_rx_cb    = NULL;
static bt_uart_connect_callback_t g_conn_cb = NULL;

bool bt_uart_service_init(void)
{
    if (g_bt_initialized) return true;
    g_bt_prev_connected = false;
    g_rx_cb             = NULL;
    g_conn_cb           = NULL;
    if (!hal_bt_init()) return false;
    g_bt_initialized = true;
    return true;
}

void bt_uart_service_deinit(void)
{
    if (!g_bt_initialized) return;
    hal_bt_deinit();
    g_bt_initialized    = false;
    g_bt_prev_connected = false;
    g_rx_cb             = NULL;
    g_conn_cb           = NULL;
}

uint16_t bt_uart_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !g_bt_initialized) return 0;
    return (uint16_t)hal_bt_spp_write(data, (int)len);
}

uint16_t bt_uart_send_string(const char *str)
{
    if (!str) return 0;
    return bt_uart_send((const uint8_t *)str, (uint16_t)strlen(str));
}

void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback) { g_rx_cb = callback; }
void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback) { g_conn_cb = callback; }
bool bt_uart_is_connected(void) { return hal_bt_spp_is_connected(); }
uint16_t bt_uart_get_tx_buffer_usage(void) { return 0; }
uint16_t bt_uart_get_rx_buffer_usage(void) { return (uint16_t)hal_bt_spp_available(); }

void bt_uart_poll(void)
{
    if (!g_bt_initialized) return;
    hal_bt_poll();
    bool now_connected = hal_bt_spp_is_connected();
    if (now_connected != g_bt_prev_connected) {
        g_bt_prev_connected = now_connected;
        if (g_conn_cb) g_conn_cb(now_connected);
    }
    if (g_rx_cb) {
        uint8_t buf[64];
        int n;
        while ((n = hal_bt_spp_read(buf, sizeof(buf))) > 0) {
            g_rx_cb(buf, (uint16_t)n);
        }
    }
}

#else
/* ═══ CONFIG_BT_ENABLED 未定义 — 空桩 ═══ */

bool bt_uart_service_init(void) { return false; }
void bt_uart_service_deinit(void) {}
uint16_t bt_uart_send(const uint8_t *data, uint16_t len) { (void)data; (void)len; return 0; }
uint16_t bt_uart_send_string(const char *str) { (void)str; return 0; }
void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback) { (void)callback; }
void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback) { (void)callback; }
bool bt_uart_is_connected(void) { return false; }
uint16_t bt_uart_get_tx_buffer_usage(void) { return 0; }
uint16_t bt_uart_get_rx_buffer_usage(void) { return 0; }
void bt_uart_poll(void) {}

#endif /* NATIVE_TEST / CONFIG_BT_ENABLED */
