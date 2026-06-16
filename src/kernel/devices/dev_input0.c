/**
 * @file   dev_input0.c
 * @brief  /dev/input0 按键输入设备实现（统一设备模型）
 * @details 将 HAL 输入层事件映射为 kern_device_ops_t 回调。
 *          read() 返回结构化按键事件（6 字节）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_input0.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"

#include <string.h>

/* ═══ 事件队列 ═══ */

#define INPUT0_EVENT_QUEUE_SIZE 8

static dev_input_event_t g_event_queue[INPUT0_EVENT_QUEUE_SIZE];
static uint8_t g_event_head = 0;
static uint8_t g_event_tail = 0;
static uint8_t g_event_count = 0;

static void input0_poll_events(void)
{
    for (int btn = 0; btn < HAL_BTN_COUNT; btn++) {
        hal_event_t e = hal_input_get_event((hal_button_t)btn);
        if (e == HAL_EVENT_NONE) {
            continue;
        }
        if (g_event_count >= INPUT0_EVENT_QUEUE_SIZE) {
            break;
        }
        g_event_queue[g_event_head].button    = (uint8_t)btn;
        g_event_queue[g_event_head].event     = (uint8_t)e;
        g_event_queue[g_event_head].timestamp = hal_get_ticks();
        g_event_head = (uint8_t)((g_event_head + 1) % INPUT0_EVENT_QUEUE_SIZE);
        g_event_count++;
    }
}

/* ═══ 设备回调 ═══ */

static kern_err_t dev_input0_open(kern_device_t *dev, int flags)
{
    (void)dev;
    (void)flags;
    return KERN_OK;
}

static kern_err_t dev_input0_close(kern_device_t *dev)
{
    (void)dev;
    return KERN_OK;
}

static kern_err_t dev_input0_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)offset;

    if (buf == NULL) return KERN_EINVAL;
    if (len < DEV_INPUT_EVENT_SIZE) {
        return KERN_EINVAL;
    }

    input0_poll_events();

    dev_input_event_t ev;
    memset(&ev, 0, sizeof(ev));

    if (g_event_count > 0) {
        ev = g_event_queue[g_event_tail];
        g_event_tail = (uint8_t)((g_event_tail + 1) % INPUT0_EVENT_QUEUE_SIZE);
        g_event_count--;
    }

    memcpy(buf, &ev, DEV_INPUT_EVENT_SIZE);
    return DEV_INPUT_EVENT_SIZE;
}

static kern_err_t dev_input0_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)len;
    (void)offset;
    return KERN_EINVAL;  /* 输入设备不支持写入 */
}

static kern_err_t dev_input0_ioctl(kern_device_t *dev, unsigned int cmd, unsigned long arg)
{
    (void)dev;

    switch (cmd) {
    case DEV_INPUT_IOCTL_SET_DOUBLE_CLICK:
        hal_input_set_double_click_enabled(arg != 0);
        return KERN_OK;
    default:
        return KERN_ENOTTY;
    }
}

/* ═══ 设备操作表 ═══ */

static kern_device_ops_t g_input0_ops = {
    .open  = dev_input0_open,
    .close = dev_input0_close,
    .read  = dev_input0_read,
    .write = dev_input0_write,
    .ioctl = dev_input0_ioctl,
};

/* ═══ 设备描述符 ═══ */

kern_device_t g_input0_dev = {
    .name         = "input0",
    .type         = KERN_DEV_CHAR,
    .ops          = &g_input0_ops,
    .private_data = NULL,
    .next         = NULL,
};
