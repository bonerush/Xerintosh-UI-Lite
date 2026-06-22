/**
 * @file   dev_ttyS0.cpp
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
/* TODO(phase 2.4): 迁移到 app/flasher/flasher.h 声明或改用 dev_ttyS0_set_bridge_active */
extern bool g_flasher_bridge_active;
static bool s_ttyS0_bridge_active = false;

#define TTY_RX_BUF_SIZE  512
#define TTY_TX_BUF_SIZE  512

static char  g_rx_buf[TTY_RX_BUF_SIZE];
static volatile uint16_t g_rx_head = 0;
static volatile uint16_t g_rx_tail = 0;
static volatile uint16_t g_rx_count = 0;

static char  g_tx_buf[TTY_TX_BUF_SIZE];
static volatile uint16_t g_tx_head = 0;
static volatile uint16_t g_tx_tail = 0;
static volatile uint16_t g_tx_count = 0;

#ifndef NATIVE_TEST
#include "freertos/FreeRTOS.h"
#include "hal/hal_uart.h"
#include "hal/hal_system.h"
static portMUX_TYPE g_ttyS0_mux = portMUX_INITIALIZER_UNLOCKED;
#define TTY_ENTER_CRITICAL() portENTER_CRITICAL(&g_ttyS0_mux)
#define TTY_EXIT_CRITICAL()  portEXIT_CRITICAL(&g_ttyS0_mux)
#else
#define TTY_ENTER_CRITICAL() do {} while (0)
#define TTY_EXIT_CRITICAL()  do {} while (0)
#endif

/* ═══ 桥接状态迁移辅助 ═══ */

void dev_ttyS0_set_bridge_active(bool active)
{
    s_ttyS0_bridge_active = active;
}

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

    TTY_ENTER_CRITICAL();
    size_t total = 0;
    while (total < len && g_rx_count > 0) {
        out[total++] = g_rx_buf[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1) % TTY_RX_BUF_SIZE);
        #ifndef NATIVE_TEST
        __atomic_fetch_sub(&g_rx_count, 1, __ATOMIC_RELAXED);
        #else
        g_rx_count--;
        #endif
    }
    TTY_EXIT_CRITICAL();
    return (kern_err_t)total;
}

static kern_err_t dev_ttyS0_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)offset;

    if (buf == NULL) return KERN_EINVAL;
    const char *in = (const char *)buf;

    TTY_ENTER_CRITICAL();
    size_t total = 0;
    char last = 0;  /* 跟踪上一个写入字节，用于 \n→\r\n 转换 */

    while (total < len && g_tx_count < TTY_TX_BUF_SIZE) {
        char ch = in[total];

        /* ── 终端换行转换：裸 \n → \r\n ──
         * 串口终端需要 CR+LF 组合才能正确换行。如果检测到孤立的
         * \n（前一个字符不是 \r），先在环形缓冲区中插入 \r。
         * 缓冲区只剩一个槽位时优先保留原始 \n，跳过 \r 转换（K31）。 */
        if (ch == '\n' && last != '\r') {
            if (g_tx_count + 1 < TTY_TX_BUF_SIZE) {
                g_tx_buf[g_tx_head] = '\r';
                g_tx_head = (uint16_t)((g_tx_head + 1) % TTY_TX_BUF_SIZE);
                #ifndef NATIVE_TEST
                __atomic_fetch_add(&g_tx_count, 1, __ATOMIC_RELAXED);
                #else
                g_tx_count++;
                #endif
                last = '\r';
            }
            /* 空间不足时跳过 \r，保证 \n 不丢失 */
        }

        /* 写入当前字节 */
        if (g_tx_count >= TTY_TX_BUF_SIZE) break;

        g_tx_buf[g_tx_head] = ch;
        g_tx_head = (uint16_t)((g_tx_head + 1) % TTY_TX_BUF_SIZE);
        #ifndef NATIVE_TEST
        __atomic_fetch_add(&g_tx_count, 1, __ATOMIC_RELAXED);
        #else
        g_tx_count++;
        #endif
        last = ch;
        total++;
    }

    #ifdef NATIVE_TEST
    /* native 环境没有硬件串口，将转换后的数据复制到 RX buffer 供 loopback 测试（K32）。
     * 这里直接对原始输入 in[0..total) 再做一次 \n→\r\n 转换，避免依赖 TX buffer
     * 的累积状态，保持与硬件路径一致的回环语义。 */
    char last_rx = 0;
    size_t rx_total = 0;
    while (rx_total < total && g_rx_count < TTY_RX_BUF_SIZE) {
        char ch = in[rx_total];
        if (ch == '\n' && last_rx != '\r' && g_rx_count + 1 < TTY_RX_BUF_SIZE) {
            g_rx_buf[g_rx_head] = '\r';
            g_rx_head = (uint16_t)((g_rx_head + 1) % TTY_RX_BUF_SIZE);
            g_rx_count++;
            last_rx = '\r';
        }
        if (g_rx_count < TTY_RX_BUF_SIZE) {
            g_rx_buf[g_rx_head] = ch;
            g_rx_head = (uint16_t)((g_rx_head + 1) % TTY_RX_BUF_SIZE);
            g_rx_count++;
            last_rx = ch;
            rx_total++;
        }
    }
    #endif

    TTY_EXIT_CRITICAL();
    return (kern_err_t)total;
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

/* C++17 不支持 C99 designated initializers，用全局对象构造函数完成初始化。
 * 该初始化与 g_ttyS0_dev 定义在同一编译单元，顺序确定；
 * 实际使用点 kern_devices_init() 在 main() 中调用，此时已完成构造。 */
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
     * 在以下三种情况下跳过 RX，将字符留在硬件 UART 缓冲区:
     * 1. serial_input 正在等待密码/配对码（由 serial_poll() 直接消费）
     * 2. 串口监视器正在运行（由 serial_monitor_update() 直接消费）
     * 3. 烧录器有线桥接激活（由 flasher_app 的 flasher_loop 直接消费）
     * 否则 UART 字节会被此处消耗并进入 ring buffer，
     * Shell 和 serial_input/serial_monitor/flasher 会竞争同一份数据。
     */
    int rx_limit = 32;
    TTY_ENTER_CRITICAL();
    if (!serial_input_is_waiting() && !serial_monitor_is_active() &&
        !s_ttyS0_bridge_active && !g_flasher_bridge_active) {
        while (rx_limit > 0 && g_rx_count < TTY_RX_BUF_SIZE) {
            uint8_t byte;
            int n = hal_uart0_read(&byte, 1);
            if (n <= 0) break;
            g_rx_buf[g_rx_head] = (char)byte;
            g_rx_head = (uint16_t)((g_rx_head + 1) % TTY_RX_BUF_SIZE);
            __atomic_fetch_add(&g_rx_count, 1, __ATOMIC_RELAXED);
            rx_limit--;
        }
    }
    TTY_EXIT_CRITICAL();

    /*
     * TX: 从环形缓冲区读取任务写入的数据，发送到硬件串口。
     * 每次轮询最多发送 64 字节。
     */
    int tx_limit = 64;
    while (tx_limit > 0 && g_tx_count > 0) {
        TTY_ENTER_CRITICAL();
        if (g_tx_count == 0) {
            TTY_EXIT_CRITICAL();
            break;
        }
        char ch = g_tx_buf[g_tx_tail];
        g_tx_tail = (uint16_t)((g_tx_tail + 1) % TTY_TX_BUF_SIZE);
        __atomic_fetch_sub(&g_tx_count, 1, __ATOMIC_RELAXED);
        TTY_EXIT_CRITICAL();
        hal_uart0_write((const uint8_t *)&ch, 1);
        tx_limit--;
    }
}
#endif
