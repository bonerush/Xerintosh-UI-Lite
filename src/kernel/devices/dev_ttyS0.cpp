/**
 * @file   dev_ttyS0.c
 * @brief  /dev/ttyS0 串口设备实现（统一设备模型）
 * @details 通过环形缓冲区将硬件串口映射为 kern_device_ops_t 回调。
 *
 *          为避免任务上下文中的 FreeRTOS 冲突，所有 Serial 访问
 *          都在 dev_ttyS0_poll() 中执行（由主 loop 调用）。
 *          任务的 read/write 仅操作内存环形缓冲区。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_ttyS0.h"
#include "app/serial_input/serial_input.h"
#include "app/serial_monitor/serial_monitor.h"

#include <string.h>

/* 烧录器有线桥接激活时，Shell 跳过 Serial RX，避免竞争 */
extern bool g_flasher_bridge_active;

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

static char  g_tx_buf[TTY_TX_BUF_SIZE];
static volatile int g_tx_head = 0;
static volatile int g_tx_tail = 0;
static volatile int g_tx_count = 0;
#endif

/* ═══ 设备回调 ═══ */

static kern_err_t dev_ttyS0_open(kern_device_t *dev, int flags)
{
    (void)dev;
    (void)flags;
    return KERN_OK;
}

static kern_err_t dev_ttyS0_close(kern_device_t *dev)
{
    (void)dev;
    return KERN_OK;
}

static kern_err_t dev_ttyS0_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)offset;

    if (buf == NULL) return KERN_EINVAL;
    char *out = (char *)buf;

#ifdef NATIVE_TEST
    if (g_tty_count == 0) return 0;
    size_t total = 0;
    while (total < len && g_tty_count > 0) {
        out[total++] = g_tty_buf[g_tty_tail];
        g_tty_tail = (g_tty_tail + 1) % TTY_BUF_SIZE;
        g_tty_count--;
    }
    return (kern_err_t)total;
#else
    size_t total = 0;
    while (total < len && g_rx_count > 0) {
        out[total++] = g_rx_buf[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % TTY_RX_BUF_SIZE;
        __atomic_fetch_sub(&g_rx_count, 1, __ATOMIC_RELAXED);
    }
    return (kern_err_t)total;
#endif
}

static kern_err_t dev_ttyS0_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)offset;

    if (buf == NULL) return KERN_EINVAL;
    const char *in = (const char *)buf;

#ifdef NATIVE_TEST
    size_t total = 0;
    while (total < len) {
        if (g_tty_count >= TTY_BUF_SIZE) break;
        g_tty_buf[g_tty_head] = in[total++];
        g_tty_head = (g_tty_head + 1) % TTY_BUF_SIZE;
        g_tty_count++;
    }
    return (kern_err_t)total;
#else
    size_t total = 0;
    while (total < len && g_tx_count < TTY_TX_BUF_SIZE) {
        g_tx_buf[g_tx_head] = in[total];
        g_tx_head = (g_tx_head + 1) % TTY_TX_BUF_SIZE;
        __atomic_fetch_add(&g_tx_count, 1, __ATOMIC_RELAXED);
        total++;
    }
    return (kern_err_t)total;
#endif
}

static kern_err_t dev_ttyS0_ioctl(kern_device_t *dev, unsigned int cmd, unsigned long arg)
{
    (void)dev;
    (void)cmd;
    (void)arg;
    return KERN_ENOTTY;  /* 串口设备暂不支持 ioctl */
}

/* ═══ 设备操作表 ═══ */

static kern_device_ops_t g_ttyS0_ops = {
    .open  = dev_ttyS0_open,
    .close = dev_ttyS0_close,
    .read  = dev_ttyS0_read,
    .write = dev_ttyS0_write,
    .ioctl = dev_ttyS0_ioctl,
};

/* ═══ 设备描述符 ═══ */

kern_device_t g_ttyS0_dev;

/* C++17 不支持 C99 designated initializers，用全局对象构造函数完成初始化 */
static struct TtyS0DevInitializer {
    TtyS0DevInitializer() {
        memset(&g_ttyS0_dev, 0, sizeof(g_ttyS0_dev));
        strncpy(g_ttyS0_dev.name, "ttyS0", KERN_NAME_MAX);
        g_ttyS0_dev.name[KERN_NAME_MAX] = '\0';
        g_ttyS0_dev.type = KERN_DEV_CHAR;
        g_ttyS0_dev.ops  = &g_ttyS0_ops;
    }
} g_ttyS0_dev_initializer;

/* ═══ 设备轮询（仅 ESP32） ═══ */

#ifndef NATIVE_TEST
void dev_ttyS0_poll(void)
{
    /*
     * RX: 从硬件串口读取数据，写入环形缓冲区供任务消费。
     *
     * 在以下三种情况下跳过 RX，将字符留在硬件 Serial 缓冲区:
     * 1. serial_input 正在等待密码/配对码（由 serial_poll() 直接消费）
     * 2. 串口监视器正在运行（由 serial_monitor_update() 直接消费）
     * 3. 烧录器有线桥接激活（由 flasher_app 的 flasher_loop 直接消费）
     * 否则 Serial 字节会被此处消耗并进入 ring buffer，
     * Shell 和 serial_input/serial_monitor/flasher 会竞争同一份数据。
     */
    int rx_limit = 32;
    if (!serial_input_is_waiting() && !serial_monitor_is_active() && !g_flasher_bridge_active) {
        while (rx_limit > 0 && Serial.available() > 0 && g_rx_count < TTY_RX_BUF_SIZE) {
            g_rx_buf[g_rx_head] = (char)Serial.read();
            g_rx_head = (g_rx_head + 1) % TTY_RX_BUF_SIZE;
            __atomic_fetch_add(&g_rx_count, 1, __ATOMIC_RELAXED);
            rx_limit--;
        }
    }

    /*
     * TX: 从环形缓冲区读取任务写入的数据，发送到硬件串口。
     * 每次轮询最多发送 64 字节。
     */
    int tx_limit = 64;
    while (tx_limit > 0 && g_tx_count > 0) {
        Serial.write((uint8_t)g_tx_buf[g_tx_tail]);
        g_tx_tail = (g_tx_tail + 1) % TTY_TX_BUF_SIZE;
        __atomic_fetch_sub(&g_tx_count, 1, __ATOMIC_RELAXED);
        tx_limit--;
    }
}
#endif
