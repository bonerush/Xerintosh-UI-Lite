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
static volatile bool             g_connected  = false;

#ifdef NATIVE_TEST

/* ═══ NATIVE_TEST 桩实现 ═══ */

bt_uart_err_t bt_uart_service_init(void)
{
    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected = false;
    g_rx_cb     = NULL;
    g_conn_cb   = NULL;
    return BT_UART_OK;
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
 * 内存策略：WiFi 默认开启（g_wifi_on=true），BT 延迟到内核任务 spawn 后初始化。
 *
 * 线程安全策略：
 * - g_bt_serial 的 connected()/read()/write() 必须在 Arduino 主任务中调用
 *   （与 begin() 同一任务上下文），避免 Bluedroid 跨任务崩溃。
 * - RX 数据通过 FreeRTOS 队列传递给 UI 任务，消除 sm_buffer 跨任务竞争。 */

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "esp_bt.h"
#include "esp_bt_main.h"

/* ═══ 调试开关 ═══ */
#define BT_DBG_ENABLED 0

/* 内存守卫阈值
 * Bluedroid SPP begin() 实际需要约 45-50KB 连续堆内存，保留 5KB 余量。 */
#define BT_UART_MIN_FREE_HEAP       45000
#define BT_UART_MIN_MAX_ALLOC_HEAP  22000

static BluetoothSerial g_bt_serial;
static bool g_initialized    = false;
static volatile bool g_prev_connected = false;

/* RX 数据跨任务传递队列（主任务写入，UI 任务消费） */
#define BT_RX_QUEUE_SIZE 512
static QueueHandle_t g_rx_queue = NULL;

/* poll 完成信号量：deinit 等待当前正在执行的 bt_uart_poll() 结束 */
static SemaphoreHandle_t g_poll_done_sem = NULL;

bt_uart_err_t bt_uart_service_init(void)
{
    if (g_initialized) {
        Serial.println("[BT] bt_uart_service_init: already initialized");
        return BT_UART_OK;
    }

    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected      = false;
    g_prev_connected = false;
    /* 回调由调用方（如 serial_monitor/flasher）管理，不在 init 中清除。
     * 调用方应在 bt_mgr_request_enable() 之前注册回调，init 后将生效。 */

    /* 创建 RX 数据跨任务队列 */
    if (!g_rx_queue) {
        g_rx_queue = xQueueCreate(BT_RX_QUEUE_SIZE, sizeof(uint8_t));
    }

    /* 创建 poll 完成信号量，初始可用 */
    if (!g_poll_done_sem) {
        g_poll_done_sem = xSemaphoreCreateBinary();
    }
    if (g_poll_done_sem) {
        xSemaphoreGive(g_poll_done_sem);
    }

    /* 内存守卫 */
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t max_alloc = ESP.getMaxAllocHeap();
    if (free_heap < BT_UART_MIN_FREE_HEAP || max_alloc < BT_UART_MIN_MAX_ALLOC_HEAP) {
        Serial.printf("[BT-INIT] ERR_HEAP free=%u max_alloc=%u\n", free_heap, max_alloc);
        return BT_UART_ERR_HEAP;
    }

    Serial.printf("[BT-INIT] begin() start free_heap=%u max_alloc=%u ms=%lu\n",
                  free_heap, max_alloc, (unsigned long)millis());
    Serial.flush();

    uint32_t t0 = millis();
    bool ok = g_bt_serial.begin("M5Stick-P1");
    uint32_t elapsed = millis() - t0;

    Serial.printf("[BT-INIT] begin()=%d took=%lums free_heap=%u max_alloc=%u\n",
                  (int)ok, (unsigned long)elapsed, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    Serial.flush();

    if (!ok) {
        Serial.println("[BT] BluetoothSerial.begin() failed");
        /* 尝试推断原因 */
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            return BT_UART_ERR_RADIO;
        }
        return BT_UART_ERR_BLUEDROID;
    }

    g_initialized = true;

    /*
     * ═══ 稳定延迟 ═══
     * begin() 返回 true 后，Bluedroid 仍需时间完成 SPP 服务注册与
     * BR/EDR inquiry scan 的启动。若立即返回，设备尚未进入可被发现状态，
     * 导致 Mac 端搜索不到 "M5Stick-P1"。
     *
     * 500ms 延迟允许 Bluedroid 完成：
     *   1. SPP RFCOMM 通道注册
     *   2. BR/EDR 查询扫描 (inquiry scan) 启动
     *   3. 寻呼扫描 (page scan) 启动
     * （参考 ESP-IDF BT SPP initiator demo 中的 post-init delay） */
    delay(500);
    Serial.printf("[BT-INIT] post-begin delay done, ready. free_heap=%u\n",
                  ESP.getFreeHeap());
    Serial.flush();

    return BT_UART_OK;
}

void bt_uart_service_deinit(void)
{
    if (!g_initialized) return;

    /* ═══ 线程安全：先关标记，阻止 bt_uart_poll() 继续使用队列 ═══ */
    g_initialized = false;

    /* 等待当前正在执行的 bt_uart_poll() 给出完成信号，超时 100ms。
     * poll 与 deinit 均在 loop() 任务中，此处阻塞不会导致死锁。 */
    if (g_poll_done_sem) {
        xSemaphoreTake(g_poll_done_sem, pdMS_TO_TICKS(100));
    }

    g_bt_serial.end();

    /* BluetoothSerial.end() 可能未完全释放 Bluedroid 栈，
     * 导致下次 begin() 时栈状态不一致，SPP 连接无法建立。
     * 手动释放 Bluedroid + BT Controller，确保下次 begin() 从零初始化。 */
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    /* 注意：g_initialized 已在上方提前置 false */
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
    if (!g_initialized) {
        if (g_poll_done_sem) {
            xSemaphoreGive(g_poll_done_sem);
        }
        return;
    }

    /* 标记 poll 进入临界区；deinit 会等待此信号量 */
    if (g_poll_done_sem) {
        xSemaphoreTake(g_poll_done_sem, 0);
    }

    /* ── 检查连接状态 ── */
    bool now_connected = g_bt_serial.connected();

    /* 打印连接状态变化或每 50 次 poll 打印一次 */
    static uint32_t s_poll_cnt = 0;
    s_poll_cnt++;
    bool print_state = (now_connected != g_prev_connected) || (s_poll_cnt % 50 == 1);

    if (print_state) {
        Serial.printf("[BT-POLL] poll#%lu connected=%d initialized=%d free_heap=%u\n",
                      (unsigned long)s_poll_cnt, (int)now_connected,
                      (int)g_initialized, ESP.getFreeHeap());
        Serial.flush();
    }

    if (now_connected != g_prev_connected) {
        g_connected = now_connected;
        g_prev_connected = now_connected;
        if (g_conn_cb) {
            Serial.printf("[BT-POLL] firing connect_cb(%d)\n", (int)now_connected);
            Serial.flush();
            g_conn_cb(now_connected);
        } else {
            Serial.printf("[BT-POLL] no connect_cb registered, skipping\n");
            Serial.flush();
        }
    }

    /* ── 读取 RX 数据 ──
     * 限制单次 poll 最大字节数 + 间歇 yield，防止长时间阻塞 loop()
     * 导致 FreeRTOS idle 任务饿死 → TWDT 复位。 */
    int rx_count = 0;
    const int RX_MAX_PER_POLL = 256;

    while (g_bt_serial.available()) {
        int b = g_bt_serial.read();
        if (b >= 0) {
            rx_count++;
            uint8_t byte = (uint8_t)b;
            if (g_rx_queue) {
                if (xQueueSend(g_rx_queue, &byte, 0) != pdTRUE) {
                    Serial.println("[BT] WARN: RX queue full, dropping byte");
                    Serial.flush();
                }
            }
        }

        /* 上限保护 + 每 32 字节 yield */
        if (rx_count >= RX_MAX_PER_POLL) {
            break;
        }
        if ((rx_count & 0x1F) == 0) {
            delay(0);  /* taskYIELD — 给 FreeRTOS idle 喂狗机会 */
        }
    }

    /* 标记 poll 离开临界区 */
    if (g_poll_done_sem) {
        xSemaphoreGive(g_poll_done_sem);
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
