/**
 * @file   dev_pwrkey.c
 * @brief  /dev/pwrkey 电源键设备实现
 * @details 将 HAL 电源键事件映射为 VFS 文件操作。
 *          read() 返回结构化电源键事件（9 字节）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kernel/devices/dev_pwrkey.h"
#include "hal/hal_power_key.h"
#include "hal/hal_system.h"

#include <string.h>

/* ═══ read ═══ */

static ssize_t dev_pwrkey_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;

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

/* ═══ release ═══ */

static int dev_pwrkey_release(kern_file_t *f)
{
    (void)f;
    return KERN_OK;
}

/* ═══ 操作表 ═══ */

static kern_file_ops_t g_dev_pwrkey_fops = {
    .read    = dev_pwrkey_read,
    .write   = NULL,
    .ioctl   = NULL,
    .release = dev_pwrkey_release,
};

kern_file_ops_t *dev_pwrkey_get_fops(void)
{
    return &g_dev_pwrkey_fops;
}
