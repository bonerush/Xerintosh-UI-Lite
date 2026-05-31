/**
 * @file   bt_uart_service.cpp
 * @brief  BLE UART 服务实现（Nordic UART Service）
 * @details 双实现架构：
 *          - NATIVE_TEST 时：提供可测试的桩实现（环形缓冲区在桩中可用）
 *          - 硬件环境时：基于 NimBLE 的完整 NUS 实现，
 *            支持 notify 发送、write 接收、环形缓冲区及分块传输。
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

static uint16_t ringbuf_read(bt_uart_ringbuf_t *rb,
                              uint8_t *out, uint16_t max_len)
{
    uint16_t read = 0;
    while (read < max_len && rb->count > 0) {
        out[read] = rb->data[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        rb->count--;
        read++;
    }
    return read;
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

/* ═══ NimBLE 硬件实现 ═══ */

#include <NimBLEDevice.h>

/* NUS UUID 定义 */
#define NUS_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_TX_UUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/* BLE 对象指针 */
static NimBLEServer         *g_server       = NULL;
static NimBLEService        *g_service      = NULL;
static NimBLECharacteristic *g_tx_char      = NULL;  /* Notify: 设备→手机 */
static NimBLECharacteristic *g_rx_char      = NULL;  /* Write:  手机→设备 */
static uint16_t              g_negotiated_mtu = BT_UART_DEFAULT_MTU;
static bool                  g_initialized   = false;

/* ═══ NimBLE 回调 ═══ */

/**
 * @brief NUS 服务器连接/断开回调
 */
class UartServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer) {
        g_connected = true;
        /* 更新协商 MTU（减去 3 字节 ATT 头） */
        g_negotiated_mtu = pServer->getPeerMTU(0) > 3
                         ? pServer->getPeerMTU(0) - 3
                         : BT_UART_DEFAULT_MTU;
        if (g_conn_cb) g_conn_cb(true);
    }

    void onDisconnect(NimBLEServer *pServer) {
        g_connected = false;
        g_negotiated_mtu = BT_UART_DEFAULT_MTU;
        if (g_conn_cb) g_conn_cb(false);
        /* 重新开始广播，允许重连 */
        pServer->startAdvertising();
    }
};

/**
 * @brief NUS RX 特征写入回调（手机→设备）
 */
class UartRxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.empty()) return;

        const uint8_t *data = (const uint8_t *)value.data();
        uint16_t len = (uint16_t)value.size();

        ringbuf_write(&g_rx_buf, data, len);
        if (g_rx_cb) g_rx_cb(data, len);
    }
};

/* ═══ 公共 API 实现 ═══ */

bool bt_uart_service_init(void)
{
    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected   = false;
    g_initialized = false;

    NimBLEServer *server = NimBLEDevice::getServer();
    if (!server) {
        /* 若 bt_manager 未创建服务器，此处创建 */
        server = NimBLEDevice::createServer();
    }
    if (!server) return false;

    server->setCallbacks(new UartServerCallbacks());
    g_server = server;

    /* 创建 NUS 服务 */
    NimBLEService *service = server->createService(NUS_SERVICE_UUID);
    if (!service) return false;
    g_service = service;

    /* TX 特征（设备→手机，Notify） */
    g_tx_char = service->createCharacteristic(
        NUS_CHAR_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );
    if (!g_tx_char) return false;

    /* RX 特征（手机→设备，Write / WriteNoResponse） */
    g_rx_char = service->createCharacteristic(
        NUS_CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    if (!g_rx_char) return false;
    g_rx_char->setCallbacks(new UartRxCallbacks());

    /* 启动服务 */
    if (!service->start()) return false;

    /* 配置广播 */
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->start();

    g_initialized = true;
    return true;
}

void bt_uart_service_deinit(void)
{
    if (g_service) {
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) adv->stop();

        /* 删除服务及其特征（NimBLE 内部管理内存） */
        g_server->removeService(g_service, true);
        g_service = NULL;
        g_tx_char = NULL;
        g_rx_char = NULL;
        g_server  = NULL;
    }
    g_connected   = false;
    g_initialized = false;
    g_rx_cb       = NULL;
    g_conn_cb     = NULL;
}

uint16_t bt_uart_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !g_connected || !g_tx_char) return 0;

    /* MTU 受限：notify 单包最大 = negotiated_mtu - 3 */
    uint16_t mtu = g_negotiated_mtu > 3 ? g_negotiated_mtu - 3
                                         : BT_UART_DEFAULT_MTU;
    uint16_t sent = 0;

    while (sent < len) {
        uint16_t chunk = len - sent;
        if (chunk > mtu) chunk = mtu;

        g_tx_char->setValue((uint8_t *)(data + sent), chunk);
        g_tx_char->notify();

        sent += chunk;
    }

    return sent;
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

#endif /* NATIVE_TEST */
