/**
 * @file   dev_input0.c
 * @brief  /dev/input0 按键输入设备实现
 * @details 将 HAL 输入层事件映射为 VFS 文件操作。
 *          read() 返回结构化按键事件（6 字节）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_input0.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"

#include <string.h>

/* ═══ read ═══ */

static ssize_t dev_input0_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;

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

/* ═══ ioctl ═══ */

static int dev_input0_ioctl(kern_file_t *f, unsigned int cmd, unsigned long arg)
{
    (void)f;

    switch (cmd) {
    case DEV_INPUT_IOCTL_SET_DOUBLE_CLICK:
        hal_input_set_double_click_enabled(arg != 0);
        return KERN_OK;
    default:
        return KERN_ENOTTY;
    }
}

/* ═══ release ═══ */

static int dev_input0_release(kern_file_t *f)
{
    (void)f;
    return KERN_OK;
}

/* ═══ 操作表 ═══ */

static kern_file_ops_t g_dev_input0_fops = {
    .read    = dev_input0_read,
    .write   = NULL,
    .ioctl   = dev_input0_ioctl,
    .release = dev_input0_release,
};

kern_file_ops_t *dev_input0_get_fops(void)
{
    return &g_dev_input0_fops;
}
