/**
 * @file   dev_ttyS0.c
 * @brief  /dev/ttyS0 串口设备实现
 * @details 通过环形缓冲区将硬件串口映射为 VFS 文件操作。
 *
 *          为避免任务上下文中的 FreeRTOS 冲突，所有 Serial 访问
 *          都在 dev_ttyS0_poll() 中执行（由主 loop 调用）。
 *          任务的 read/write 仅操作内存环形缓冲区。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_ttyS0.h"

#ifdef NATIVE_TEST
#define TTY_BUF_SIZE  512

static char  g_tty_buf[TTY_BUF_SIZE];
static int   g_tty_head = 0;
static int   g_tty_tail = 0;
static int   g_tty_count = 0;
#else
#include <Arduino.h>

#define TTY_RX_BUF_SIZE  512
#define TTY_TX_BUF_SIZE  512

static char  g_rx_buf[TTY_RX_BUF_SIZE];
static volatile int g_rx_head = 0;
static volatile int g_rx_tail = 0;
static volatile int g_rx_count = 0;
static portMUX_TYPE g_rx_spinlock = portMUX_INITIALIZER_UNLOCKED;

static char  g_tx_buf[TTY_TX_BUF_SIZE];
static volatile int g_tx_head = 0;
static volatile int g_tx_tail = 0;
static volatile int g_tx_count = 0;
static portMUX_TYPE g_tx_spinlock = portMUX_INITIALIZER_UNLOCKED;
#endif

/* ═══ read ═══ */

static ssize_t dev_ttyS0_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;

#ifdef NATIVE_TEST
    if (g_tty_count == 0) return 0;
    size_t total = 0;
    while (total < len && g_tty_count > 0) {
        buf[total++] = g_tty_buf[g_tty_tail];
        g_tty_tail = (g_tty_tail + 1) % TTY_BUF_SIZE;
        g_tty_count--;
    }
    return (ssize_t)total;
#else
    size_t total = 0;
    while (total < len && g_rx_count > 0) {
        buf[total++] = g_rx_buf[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % TTY_RX_BUF_SIZE;
        portENTER_CRITICAL(&g_rx_spinlock);
        g_rx_count--;
        portEXIT_CRITICAL(&g_rx_spinlock);
    }
    return (ssize_t)total;
#endif
}

/* ═══ write ═══ */

static ssize_t dev_ttyS0_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;

#ifdef NATIVE_TEST
    size_t total = 0;
    while (total < len) {
        if (g_tty_count >= TTY_BUF_SIZE) break;
        g_tty_buf[g_tty_head] = buf[total++];
        g_tty_head = (g_tty_head + 1) % TTY_BUF_SIZE;
        g_tty_count++;
    }
    return (ssize_t)total;
#else
    size_t total = 0;
    while (total < len && g_tx_count < TTY_TX_BUF_SIZE) {
        g_tx_buf[g_tx_head] = buf[total];
        g_tx_head = (g_tx_head + 1) % TTY_TX_BUF_SIZE;
        portENTER_CRITICAL(&g_tx_spinlock);
        g_tx_count++;
        portEXIT_CRITICAL(&g_tx_spinlock);
        total++;
    }
    return (ssize_t)total;
#endif
}

/* ═══ release ═══ */

static int dev_ttyS0_release(kern_file_t *f)
{
    (void)f;
    return KERN_OK;
}

/* ═══ 操作表 ═══ */

static kern_file_ops_t g_dev_ttyS0_fops = {
    .read    = dev_ttyS0_read,
    .write   = dev_ttyS0_write,
    .ioctl   = NULL,
    .release = dev_ttyS0_release,
};

kern_file_ops_t *dev_ttyS0_get_fops(void)
{
    return &g_dev_ttyS0_fops;
}

/* ═══ 设备轮询（仅 ESP32） ═══ */

#ifndef NATIVE_TEST
void dev_ttyS0_poll(void)
{
    /*
     * RX: 从硬件串口读取数据，写入环形缓冲区供任务消费。
     * 每次轮询最多读取 32 字节，避免阻塞主循环太久。
     */
    int rx_limit = 32;
    while (rx_limit > 0 && Serial.available() > 0 && g_rx_count < TTY_RX_BUF_SIZE) {
        g_rx_buf[g_rx_head] = (char)Serial.read();
        g_rx_head = (g_rx_head + 1) % TTY_RX_BUF_SIZE;
        portENTER_CRITICAL(&g_rx_spinlock);
        g_rx_count++;
        portEXIT_CRITICAL(&g_rx_spinlock);
        rx_limit--;
    }

    /*
     * TX: 从环形缓冲区读取任务写入的数据，发送到硬件串口。
     * 每次轮询最多发送 64 字节。
     */
    int tx_limit = 64;
    while (tx_limit > 0 && g_tx_count > 0) {
        Serial.write((uint8_t)g_tx_buf[g_tx_tail]);
        g_tx_tail = (g_tx_tail + 1) % TTY_TX_BUF_SIZE;
        portENTER_CRITICAL(&g_tx_spinlock);
        g_tx_count--;
        portEXIT_CRITICAL(&g_tx_spinlock);
        tx_limit--;
    }
}
#endif
