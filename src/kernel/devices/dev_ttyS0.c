/**
 * @file   dev_ttyS0.c
 * @brief  /dev/ttyS0 串口设备实现
 * @details 将硬件串口（Serial）映射为 VFS 文件操作。
 *
 *          原生测试环境使用 512 字节环形缓冲区模拟串口，
 *          write() 写入数据进入环形缓冲区，read() 从中读取。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_ttyS0.h"

#ifdef NATIVE_TEST
#include <string.h>
#include <stdio.h>

#define TTY_BUF_SIZE  512

static char  g_tty_buf[TTY_BUF_SIZE];
static int   g_tty_head = 0;
static int   g_tty_tail = 0;
static int   g_tty_count = 0;
#else
#include <Arduino.h>
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
    while (total < len && Serial.available() > 0) {
        buf[total++] = (char)Serial.read();
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
    while (total < len) {
        Serial.write((uint8_t)buf[total]);
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
