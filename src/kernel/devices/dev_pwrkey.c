/**
 * @file   dev_pwrkey.c
 * @brief  /dev/pwrkey 电源键设备实现（新设备模型）
 * @details 实现 kern_device_ops_t 的五项回调，
 *          read() 返回结构化电源键事件（9 字节）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_pwrkey.h"
#include "hal/hal_power_key.h"
#include "hal/hal_system.h"

#include <string.h>

/* ═══ 设备回调实现 ═══ */

static kern_err_t pwrkey_open(kern_device_t *dev, int flags)
{
    (void)dev;
    (void)flags;
    return KERN_OK;
}

static kern_err_t pwrkey_close(kern_device_t *dev)
{
    (void)dev;
    return KERN_OK;
}

static kern_err_t pwrkey_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)offset;

    if (buf == NULL) return KERN_EINVAL;
    if (len < DEV_PWRKEY_EVENT_SIZE) {
        return KERN_EINVAL;
    }

    /* 轮询电源键事件 */
    dev_pwrkey_event_t ev;
    memset(&ev, 0, sizeof(ev));

    hal_pwr_key_event_t e = hal_power_key_get_event();
    if (e != HAL_PWR_KEY_NONE) {
        ev.event     = (uint8_t)e;
        ev.hold_ms   = hal_power_key_get_hold_duration_ms();
        ev.timestamp = hal_get_ticks();
    }

    memcpy(buf, &ev, DEV_PWRKEY_EVENT_SIZE);
    return DEV_PWRKEY_EVENT_SIZE;
}

static kern_err_t pwrkey_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)len;
    (void)offset;
    return KERN_EINVAL;  /* 电源键设备不支持写入 */
}

static kern_err_t pwrkey_ioctl(kern_device_t *dev, unsigned int cmd, unsigned long arg)
{
    (void)dev;
    (void)cmd;
    (void)arg;
    return KERN_ENOTTY;  /* 电源键设备不支持 ioctl */
}

/* ═══ 设备操作表 ═══ */

static kern_device_ops_t g_pwrkey_ops = {
    .open  = pwrkey_open,
    .close = pwrkey_close,
    .read  = pwrkey_read,
    .write = pwrkey_write,
    .ioctl = pwrkey_ioctl,
};

/* ═══ 设备描述符 ═══ */

kern_device_t g_pwrkey_dev = {
    .name         = "pwrkey",
    .type         = KERN_DEV_CHAR,
    .ops          = &g_pwrkey_ops,
    .private_data = NULL,
    .next         = NULL,
};
