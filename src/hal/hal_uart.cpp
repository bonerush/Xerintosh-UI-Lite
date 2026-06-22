/**
 * @file   hal_uart.cpp
 * @brief  UART 硬件抽象层实现
 * @details ESP-IDF 原生 UART 驱动封装。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_uart.h"

#ifndef NATIVE_TEST

#include "driver/uart.h"
#include "esp_err.h"

#define HAL_UART0_NUM      UART_NUM_0
#define HAL_UART0_BUF_SIZE 256

static bool s_uart0_ready = false;

void hal_uart0_init(void)
{
    if (s_uart0_ready) return;

    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(HAL_UART0_NUM, &uart_cfg);
    if (err != ESP_OK) return;

    err = uart_set_pin(HAL_UART0_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return;

    err = uart_driver_install(HAL_UART0_NUM, HAL_UART0_BUF_SIZE,
                              HAL_UART0_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return;

    s_uart0_ready = true;
}

void hal_uart0_set_baudrate(uint32_t baud_rate)
{
    if (!s_uart0_ready) hal_uart0_init();
    uart_set_baudrate(HAL_UART0_NUM, (uint32_t)baud_rate);
}

int hal_uart0_read(uint8_t *buf, int len)
{
    if (!s_uart0_ready) hal_uart0_init();
    if (buf == NULL || len <= 0) return 0;
    return (int)uart_read_bytes(HAL_UART0_NUM, buf, (size_t)len, 0);
}

int hal_uart0_write(const uint8_t *data, int len)
{
    if (!s_uart0_ready) hal_uart0_init();
    if (data == NULL || len <= 0) return 0;
    return (int)uart_write_bytes(HAL_UART0_NUM, (const char *)data, (size_t)len);
}

int hal_uart0_available(void)
{
    if (!s_uart0_ready) hal_uart0_init();
    size_t len = 0;
    uart_get_buffered_data_len(HAL_UART0_NUM, &len);
    return (int)len;
}

#else /* NATIVE_TEST */

void hal_uart0_init(void) {}
void hal_uart0_set_baudrate(uint32_t baud_rate) { (void)baud_rate; }
int hal_uart0_read(uint8_t *buf, int len) { (void)buf; (void)len; return 0; }
int hal_uart0_write(const uint8_t *data, int len) { (void)data; (void)len; return len; }
int hal_uart0_available(void) { return 0; }

#endif /* NATIVE_TEST */
