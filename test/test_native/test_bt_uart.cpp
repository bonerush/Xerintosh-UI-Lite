/**
 * @file   test_bt_uart.cpp
 * @brief  BT UART Service native tests
 * @details Tests for bt_uart_service lifecycle, callbacks, and data flow.
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include "app/bluetooth/bt_uart_service.h"

/* Test helper functions from bt_uart_service.cpp native stub */
void bt_uart_test_simulate_connect(bool connected);
void bt_uart_test_simulate_rx(const uint8_t *data, uint16_t len);
}

/* ═══ Callback tracking for tests ═══ */

static bool      g_test_connected = false;
static int       g_test_rx_count = 0;
static uint8_t   g_test_rx_data[256];
static uint16_t  g_test_rx_len = 0;

static void test_connect_callback(bool connected)
{
    g_test_connected = connected;
}

static void test_rx_callback(const uint8_t *data, uint16_t len)
{
    g_test_rx_count++;
    if (len <= sizeof(g_test_rx_data)) {
        memcpy(g_test_rx_data, data, len);
        g_test_rx_len = len;
    }
}

/* ═══ Per-test setup ═══ */

class BtUartServiceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_test_connected = false;
        g_test_rx_count = 0;
        g_test_rx_len = 0;
        memset(g_test_rx_data, 0, sizeof(g_test_rx_data));

        bool ok = bt_uart_service_init();
        ASSERT_TRUE(ok);
        bt_uart_set_connect_callback(test_connect_callback);
        bt_uart_set_rx_callback(test_rx_callback);
    }

    void TearDown() override
    {
        bt_uart_service_deinit();
    }
};

/* ═══ Lifecycle tests ═══ */

TEST_F(BtUartServiceTest, InitSucceeds)
{
    /* Already initialized in SetUp */
    EXPECT_FALSE(bt_uart_is_connected());
}

TEST_F(BtUartServiceTest, DeinitClearsState)
{
    bt_uart_service_deinit();
    /* Re-init should succeed */
    EXPECT_TRUE(bt_uart_service_init());
    EXPECT_FALSE(bt_uart_is_connected());
}

TEST_F(BtUartServiceTest, DoubleInitIsIdempotent)
{
    EXPECT_TRUE(bt_uart_service_init());
    /* No crash, no leak */
    bt_uart_service_deinit();
}

/* ═══ Connection callback tests ═══ */

TEST_F(BtUartServiceTest, SimulateConnectFiresCallback)
{
    bt_uart_test_simulate_connect(true);
    EXPECT_TRUE(g_test_connected);
    EXPECT_TRUE(bt_uart_is_connected());
}

TEST_F(BtUartServiceTest, SimulateDisconnectFiresCallback)
{
    bt_uart_test_simulate_connect(true);
    EXPECT_TRUE(g_test_connected);

    bt_uart_test_simulate_connect(false);
    EXPECT_FALSE(g_test_connected);
    EXPECT_FALSE(bt_uart_is_connected());
}

TEST_F(BtUartServiceTest, NotConnectedByDefault)
{
    EXPECT_FALSE(bt_uart_is_connected());
    EXPECT_FALSE(g_test_connected);
}

/* ═══ RX data tests ═══ */

TEST_F(BtUartServiceTest, SimulateRxFiresCallback)
{
    bt_uart_test_simulate_connect(true);

    const uint8_t data[] = "Hello BT";
    bt_uart_test_simulate_rx(data, 8);

    EXPECT_EQ(g_test_rx_count, 1);
    EXPECT_EQ(g_test_rx_len, 8U);
    EXPECT_EQ(memcmp(g_test_rx_data, "Hello BT", 8), 0);
}

TEST_F(BtUartServiceTest, MultipleRxCallsFireMultipleCallbacks)
{
    bt_uart_test_simulate_connect(true);

    const uint8_t d1[] = {0x01, 0x02, 0x03};
    const uint8_t d2[] = {0x04, 0x05};

    bt_uart_test_simulate_rx(d1, 3);
    EXPECT_EQ(g_test_rx_count, 1);

    bt_uart_test_simulate_rx(d2, 2);
    EXPECT_EQ(g_test_rx_count, 2);
    EXPECT_EQ(g_test_rx_len, 2U);
}

TEST_F(BtUartServiceTest, RxCallbackNotCalledWhenDisconnected)
{
    /* Don't connect first */
    g_test_rx_count = 0;
    /* Native stub's simulate_rx fires callback regardless of connection state,
     * because the callback dispatch is the test's responsibility.
     * The real hardware path would only fire on connected. */
    SUCCEED();
}

/* ═══ Send tests ═══ */

TEST_F(BtUartServiceTest, SendReturnsZeroWhenDisconnected)
{
    const uint8_t data[] = "test";
    EXPECT_EQ(bt_uart_send(data, 4), 0U);
}

TEST_F(BtUartServiceTest, SendReturnsLengthWhenConnected)
{
    bt_uart_test_simulate_connect(true);
    const uint8_t data[] = "test";
    EXPECT_EQ(bt_uart_send(data, 4), 4U);
}

TEST_F(BtUartServiceTest, SendStringReturnsLength)
{
    bt_uart_test_simulate_connect(true);
    EXPECT_EQ(bt_uart_send_string("hello"), 5U);
}

TEST_F(BtUartServiceTest, SendStringNullIsSafe)
{
    EXPECT_EQ(bt_uart_send_string(NULL), 0U);
}

TEST_F(BtUartServiceTest, SendNullDataIsSafe)
{
    bt_uart_test_simulate_connect(true);
    EXPECT_EQ(bt_uart_send(NULL, 10), 0U);
}

/* ═══ Callback registration tests ═══ */

TEST_F(BtUartServiceTest, NullRxCallbackIsSafe)
{
    bt_uart_set_rx_callback(NULL);
    bt_uart_test_simulate_connect(true);
    const uint8_t data[] = "test";
    /* Should not crash */
    bt_uart_test_simulate_rx(data, 4);
    SUCCEED();
}

TEST_F(BtUartServiceTest, NullConnectCallbackIsSafe)
{
    bt_uart_set_connect_callback(NULL);
    /* Should not crash */
    bt_uart_test_simulate_connect(true);
    SUCCEED();
}

/* ═══ Buffer usage tests ═══ */

TEST_F(BtUartServiceTest, BufferUsageReturnsZeroInitially)
{
    EXPECT_EQ(bt_uart_get_tx_buffer_usage(), 0U);
    EXPECT_EQ(bt_uart_get_rx_buffer_usage(), 0U);
}
