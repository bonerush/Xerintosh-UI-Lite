/**
 * @file   bt_uart_service.cpp
 * @brief  蓝牙串口服务实现（Classic Bluetooth SPP）
 * @details 双实现架构：
 *          - NATIVE_TEST 时：提供可测试的桩实现（环形缓冲区在桩中可用）
 *          - 硬件环境时：基于 BluetoothSerial 的完整 SPP 实现，
 *            支持 RFCOMM 流式发送/接收、环形缓冲区及连接状态轮询。
 *
 * @copyright Copyright (c) 2026
 */

#include "app/bluetooth/bt_uart_service.h"
#include <string.h>

/* ═══ 环形缓冲区（双实现共享） ═══ */

/**
 * @brief 字节级环形缓冲区
 */
typedef struct {
    uint8_t  data[BT_UART_TX_BUF_SIZE];  /* 数据存储 */
    uint16_t size;                         /* 缓冲区容量 */
    uint16_t head;                         /* 写入位置 */
    uint16_t tail;                         /* 读取位置 */
    uint16_t count;                        /* 已用字节数 */
} bt_uart_ringbuf_t;

static void ringbuf_init(bt_uart_ringbuf_t *rb, uint16_t size)
{
    rb->size  = size;
    rb->head  = 0;
    rb->tail  = 0;
    rb->count = 0;
}

static uint16_t ringbuf_write(bt_uart_ringbuf_t *rb,
                               const uint8_t *data, uint16_t len)
{
    uint16_t written = 0;
    while (written < len && rb->count < rb->size) {
        rb->data[rb->head] = data[written];
        rb->head = (rb->head + 1) % rb->size;
        rb->count++;
        written++;
    }
    return written;
}

static uint16_t ringbuf_peek(const bt_uart_ringbuf_t *rb,
                              uint8_t *out, uint16_t max_len)
{
    uint16_t read = 0;
    uint16_t pos  = rb->tail;
    while (read < max_len && read < rb->count) {
        out[read] = rb->data[pos];
        pos = (pos + 1) % rb->size;
        read++;
    }
    return read;
}

static uint16_t ringbuf_consume(bt_uart_ringbuf_t *rb, uint16_t len)
{
    uint16_t consumed = 0;
    while (consumed < len && rb->count > 0) {
        rb->tail = (rb->tail + 1) % rb->size;
        rb->count--;
        consumed++;
    }
    return consumed;
}

/* ═══ 模块内部状态 ═══ */

static bt_uart_ringbuf_t         g_tx_buf;
static bt_uart_ringbuf_t         g_rx_buf;
static bt_uart_rx_callback_t     g_rx_cb     = NULL;
static bt_uart_connect_callback_t g_conn_cb  = NULL;
static bool                      g_connected  = false;

#ifdef NATIVE_TEST

/* ═══ NATIVE_TEST 桩实现 ═══ */

bool bt_uart_service_init(void)
{
    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected = false;
    g_rx_cb     = NULL;
    g_conn_cb   = NULL;
    return true;
}

void bt_uart_service_deinit(void)
{
    g_connected = false;
    g_rx_cb     = NULL;
    g_conn_cb   = NULL;
}

uint16_t bt_uart_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !g_connected) return 0;
    return ringbuf_write(&g_tx_buf, data, len);
}

uint16_t bt_uart_send_string(const char *str)
{
    if (!str) return 0;
    return bt_uart_send((const uint8_t *)str, (uint16_t)strlen(str));
}

void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback)
{
    g_rx_cb = callback;
}

void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback)
{
    g_conn_cb = callback;
}

bool bt_uart_is_connected(void)
{
    return g_connected;
}

uint16_t bt_uart_get_tx_buffer_usage(void)
{
    return g_tx_buf.count;
}

uint16_t bt_uart_get_rx_buffer_usage(void)
{
    return g_rx_buf.count;
}

void bt_uart_poll(void) {}

void bt_uart_drain_rx_queue(void) {}

/* ═══ 测试辅助函数（仅 NATIVE_TEST，extern "C" 供测试链接） ═══ */

extern "C" {

/**
 * @brief 模拟客户端连接/断开（供测试调用）
 */
void bt_uart_test_simulate_connect(bool connected)
{
    g_connected = connected;
    if (g_conn_cb) g_conn_cb(connected);
}

/**
 * @brief 模拟接收数据（供测试调用）
 */
void bt_uart_test_simulate_rx(const uint8_t *data, uint16_t len)
{
    ringbuf_write(&g_rx_buf, data, len);
    if (g_rx_cb) g_rx_cb(data, len);
}

/**
 * @brief 获取 TX 缓冲区中待发送的数据（供测试验证）
 */
uint16_t bt_uart_test_peek_tx(uint8_t *out, uint16_t max_len)
{
    return ringbuf_peek(&g_tx_buf, out, max_len);
}

/**
 * @brief 消费 TX 缓冲区数据（模拟 notify 发送完成）
 */
uint16_t bt_uart_test_consume_tx(uint16_t len)
{
    return ringbuf_consume(&g_tx_buf, len);
}

} /* extern "C" */

#else

/* ═══ BluetoothSerial 实现 ═══
 * ESP32 Arduino 框架在 setup() 之前就初始化了 BT controller + Bluedroid。
 * BluetoothSerial.begin() 能正确检测并复用已初始化的栈。
 * 内存策略：WiFi 默认关闭（节省 ~38KB），BT 延迟到内核任务 spawn 后初始化。
 *
 * 线程安全策略：
 * - g_bt_serial 的 connected()/read()/write() 必须在 Arduino 主任务中调用
 *   （与 begin() 同一任务上下文），避免 Bluedroid 跨任务崩溃。
 * - RX 数据通过 FreeRTOS 队列传递给 UI 任务，消除 sm_buffer 跨任务竞争。 */

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "esp_bt.h"
#include "esp_bt_main.h"

static BluetoothSerial g_bt_serial;
static bool g_initialized    = false;
static bool g_prev_connected = false;

/* RX 数据跨任务传递队列（主任务写入，UI 任务消费） */
#define BT_RX_QUEUE_SIZE 512
static QueueHandle_t g_rx_queue = NULL;

bool bt_uart_service_init(void)
{
    if (g_initialized) return true;

    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected      = false;
    g_prev_connected = false;
    g_rx_cb          = NULL;
    g_conn_cb        = NULL;

    /* 创建 RX 数据跨任务队列 */
    if (!g_rx_queue) {
        g_rx_queue = xQueueCreate(BT_RX_QUEUE_SIZE, sizeof(uint8_t));
    }

    bool ok = g_bt_serial.begin("M5Stick-P1");
    if (!ok) {
        Serial.println("[BT] BluetoothSerial.begin() failed");
        return false;
    }

    g_initialized = true;
    return true;
}

void bt_uart_service_deinit(void)
{
    if (!g_initialized) return;

    g_bt_serial.end();

    /* BluetoothSerial.end() 可能未完全释放 Bluedroid 栈，
     * 导致下次 begin() 时栈状态不一致，SPP 连接无法建立。
     * 手动释放 Bluedroid + BT Controller，确保下次 begin() 从零初始化。 */
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    g_connected      = false;
    g_prev_connected = false;
    g_initialized    = false;
    g_rx_cb          = NULL;
    g_conn_cb        = NULL;

    /* 清空并删除 RX 队列 */
    if (g_rx_queue) {
        vQueueDelete(g_rx_queue);
        g_rx_queue = NULL;
    }
}

uint16_t bt_uart_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !g_connected || !g_initialized) return 0;
    size_t written = g_bt_serial.write(data, len);
    return (uint16_t)written;
}

uint16_t bt_uart_send_string(const char *str)
{
    if (!str) return 0;
    return bt_uart_send((const uint8_t *)str, (uint16_t)strlen(str));
}

void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback)
{
    g_rx_cb = callback;
}

void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback)
{
    g_conn_cb = callback;
}

bool bt_uart_is_connected(void)
{
    return g_connected;
}

uint16_t bt_uart_get_tx_buffer_usage(void)
{
    return g_tx_buf.count;
}

uint16_t bt_uart_get_rx_buffer_usage(void)
{
    return g_rx_buf.count;
}

void bt_uart_poll(void)
{
    if (!g_initialized) return;

    bool now_connected = g_bt_serial.connected();
    if (now_connected != g_prev_connected) {
        g_connected = now_connected;
        g_prev_connected = now_connected;
        if (g_conn_cb) {
            g_conn_cb(now_connected);
        }
    }

    /* 读取 RX 数据并放入跨任务队列（不在此处调用回调） */
    while (g_bt_serial.available()) {
        int b = g_bt_serial.read();
        if (b >= 0) {
            uint8_t byte = (uint8_t)b;
            if (g_rx_queue) {
                if (xQueueSend(g_rx_queue, &byte, 0) != pdTRUE) {
                    Serial.println("[BT] WARN: RX queue full, dropping byte");
                }
            }
        }
    }
}

/**
 * @brief  消费 RX 队列并调用回调（在 UI 任务中调用）
 * @note   从 FreeRTOS 队列中取出 bt_uart_poll() 攒下的 RX 数据，
 *         以回调形式交给调用者。调用者应在自己的任务上下文中执行，
 *         避免跨任务写 sm_buffer。
 */
void bt_uart_drain_rx_queue(void)
{
    if (!g_rx_queue || !g_rx_cb) return;

    uint8_t byte;
    while (xQueueReceive(g_rx_queue, &byte, 0) == pdTRUE) {
        g_rx_cb(&byte, 1);
    }
}

#endif /* NATIVE_TEST */
