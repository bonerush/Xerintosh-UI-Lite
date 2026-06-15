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

    /* 轮询两个按键，返回第一个检测到的事件 */
    dev_input_event_t ev;
    memset(&ev, 0, sizeof(ev));

    for (int btn = 0; btn < HAL_BTN_COUNT; btn++) {
        hal_event_t e = hal_input_get_event((hal_button_t)btn);
        if (e != HAL_EVENT_NONE) {
            ev.button    = (uint8_t)btn;
            ev.event     = (uint8_t)e;
            ev.timestamp = hal_get_ticks();
            break;
        }
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
