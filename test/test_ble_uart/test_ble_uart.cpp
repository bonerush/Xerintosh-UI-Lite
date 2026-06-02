/**
 * @file   test_ble_uart.cpp
 * @brief  TDD — 蓝牙串口服务单元测试
 * @details 验证 bt_uart_service 模块的初始化、发送、接收、
 *          连接状态查询及缓冲区管理功能。
 * @note   仅在 NATIVE_TEST 环境下编译运行。
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include "app/bluetooth/bt_uart_service.h"

/* NATIVE_TEST 桩辅助函数（定义在 bt_uart_service.cpp 中） */
extern void     bt_uart_test_simulate_connect(bool connected);
extern void     bt_uart_test_simulate_rx(const uint8_t *data, uint16_t len);
extern uint16_t bt_uart_test_peek_tx(uint8_t *out, uint16_t max_len);
extern uint16_t bt_uart_test_consume_tx(uint16_t len);
}

/* ═══ 初始化测试 ═══ */

TEST(BleUartTest, InitReturnsTrue)
{
    EXPECT_TRUE(bt_uart_service_init());
}

TEST(BleUartTest, InitSetsDefaultState)
{
    bt_uart_service_init();

    EXPECT_FALSE(bt_uart_is_connected());
    EXPECT_EQ(bt_uart_get_tx_buffer_usage(), 0);
    EXPECT_EQ(bt_uart_get_rx_buffer_usage(), 0);
}

TEST(BleUartTest, DeinitResetsState)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);
    EXPECT_TRUE(bt_uart_is_connected());

    bt_uart_service_deinit();
    EXPECT_FALSE(bt_uart_is_connected());
}

/* ═══ 连接状态测试 ═══ */

TEST(BleUartTest, ConnectCallbackFires)
{
    bt_uart_service_init();

    bool last_state = false;
    bool cb_called  = false;
    bt_uart_set_connect_callback([](bool connected) {
        /* 不能直接用局部变量，改用静态变量 */
    });

    static bool s_last_state;
    static bool s_cb_called;
    s_last_state = false;
    s_cb_called  = false;

    bt_uart_set_connect_callback([](bool connected) {
        s_last_state = connected;
        s_cb_called  = true;
    });

    bt_uart_test_simulate_connect(true);
    EXPECT_TRUE(s_cb_called);
    EXPECT_TRUE(s_last_state);

    s_cb_called = false;
    bt_uart_test_simulate_connect(false);
    EXPECT_TRUE(s_cb_called);
    EXPECT_FALSE(s_last_state);

    bt_uart_set_connect_callback(NULL);
    (void)last_state;
}

TEST(BleUartTest, IsConnectedReflectsState)
{
    bt_uart_service_init();

    EXPECT_FALSE(bt_uart_is_connected());
    bt_uart_test_simulate_connect(true);
    EXPECT_TRUE(bt_uart_is_connected());
    bt_uart_test_simulate_connect(false);
    EXPECT_FALSE(bt_uart_is_connected());
}

/* ═══ 发送测试 ═══ */

TEST(BleUartTest, SendWhenDisconnectedReturnsZero)
{
    bt_uart_service_init();
    /* 未连接时发送应返回 0 */
    uint8_t data[] = {0x41, 0x42};
    EXPECT_EQ(bt_uart_send(data, sizeof(data)), 0);
}

TEST(BleUartTest, SendWhenConnectedReturnsLen)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    uint8_t data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  /* "Hello" */
    EXPECT_EQ(bt_uart_send(data, sizeof(data)), sizeof(data));
}

TEST(BleUartTest, SendStringWorks)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    uint16_t sent = bt_uart_send_string("UART");
    EXPECT_EQ(sent, 4);
}

TEST(BleUartTest, SendNullReturnsZero)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    EXPECT_EQ(bt_uart_send(NULL, 5), 0);
}

TEST(BleUartTest, SendZeroLenReturnsZero)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    uint8_t data[] = {0x01};
    EXPECT_EQ(bt_uart_send(data, 0), 0);
}

TEST(BleUartTest, SendStringNullReturnsZero)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    EXPECT_EQ(bt_uart_send_string(NULL), 0);
}

TEST(BleUartTest, SendDataAppearsInTxBuffer)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    uint8_t data[] = {0x01, 0x02, 0x03};
    bt_uart_send(data, sizeof(data));

    uint8_t peek[8];
    uint16_t peeked = bt_uart_test_peek_tx(peek, sizeof(peek));
    EXPECT_EQ(peeked, 3);
    EXPECT_EQ(memcmp(peek, data, 3), 0);
}

TEST(BleUartTest, TxBufferUsageTracking)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    EXPECT_EQ(bt_uart_get_tx_buffer_usage(), 0);

    uint8_t data[10] = {0};
    bt_uart_send(data, 10);
    EXPECT_EQ(bt_uart_get_tx_buffer_usage(), 10);

    bt_uart_test_consume_tx(5);
    EXPECT_EQ(bt_uart_get_tx_buffer_usage(), 5);
}

/* ═══ 接收测试 ═══ */

TEST(BleUartTest, RxFiresCallback)
{
    bt_uart_service_init();

    static uint8_t s_rx_buf[64];
    static uint16_t s_rx_len;
    s_rx_len = 0;

    bt_uart_set_rx_callback([](const uint8_t *data, uint16_t len) {
        memcpy(s_rx_buf, data, len);
        s_rx_len = len;
    });

    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    bt_uart_test_simulate_rx(payload, sizeof(payload));

    EXPECT_EQ(s_rx_len, 3);
    EXPECT_EQ(s_rx_buf[0], 0xAA);
    EXPECT_EQ(s_rx_buf[1], 0xBB);
    EXPECT_EQ(s_rx_buf[2], 0xCC);

    bt_uart_set_rx_callback(NULL);
}

TEST(BleUartTest, RxDataInBuffer)
{
    bt_uart_service_init();

    bt_uart_set_rx_callback(NULL);  /* 不注册回调 */

    uint8_t payload[] = {0x10, 0x20};
    bt_uart_test_simulate_rx(payload, sizeof(payload));

    EXPECT_EQ(bt_uart_get_rx_buffer_usage(), 2);
}

TEST(BleUartTest, NoCallbackWhenNull)
{
    bt_uart_service_init();
    bt_uart_set_rx_callback(NULL);

    /* 不应崩溃 */
    uint8_t payload[] = {0xFF};
    bt_uart_test_simulate_rx(payload, 1);
    EXPECT_EQ(bt_uart_get_rx_buffer_usage(), 1);
}

/* ═══ 缓冲区溢出测试 ═══ */

TEST(BleUartTest, TxBufferOverflowDropsOldest)
{
    bt_uart_service_init();
    bt_uart_test_simulate_connect(true);

    /* 填满整个 TX 缓冲区 */
    uint8_t fill[BT_UART_TX_BUF_SIZE];
    memset(fill, 0x11, sizeof(fill));
    EXPECT_EQ(bt_uart_send(fill, sizeof(fill)), BT_UART_TX_BUF_SIZE);
    EXPECT_EQ(bt_uart_get_tx_buffer_usage(), BT_UART_TX_BUF_SIZE);

    /* 再写入应返回 0（缓冲区满） */
    uint8_t extra[] = {0xFF};
    EXPECT_EQ(bt_uart_send(extra, 1), 0);
}

TEST(BleUartTest, RxBufferOverflowDropsOldest)
{
    bt_uart_service_init();

    /* 写入超过 RX 缓冲区大小 */
    uint8_t big[BT_UART_RX_BUF_SIZE + 64];
    memset(big, 0x42, sizeof(big));
    bt_uart_test_simulate_rx(big, sizeof(big));

    /* 已用量应等于缓冲区容量 */
    EXPECT_EQ(bt_uart_get_rx_buffer_usage(), BT_UART_RX_BUF_SIZE);
}

/* ═══ 回调注册测试 ═══ */

TEST(BleUartTest, SetRxCallbackNullUnregisters)
{
    bt_uart_service_init();

    static bool s_called;
    s_called = false;

    bt_uart_set_rx_callback([](const uint8_t *d, uint16_t l) {
        (void)d; (void)l;
        s_called = true;
    });

    bt_uart_set_rx_callback(NULL);

    uint8_t data[] = {0x01};
    bt_uart_test_simulate_rx(data, 1);
    EXPECT_FALSE(s_called);
}

TEST(BleUartTest, SetConnectCallbackNullUnregisters)
{
    bt_uart_service_init();

    static bool s_called;
    s_called = false;

    bt_uart_set_connect_callback([](bool c) {
        (void)c;
        s_called = true;
    });

    bt_uart_set_connect_callback(NULL);

    bt_uart_test_simulate_connect(true);
    EXPECT_FALSE(s_called);
}
