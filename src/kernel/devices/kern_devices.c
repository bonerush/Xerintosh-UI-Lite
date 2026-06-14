/**
 * @file   kern_devices.c
 * @brief  物理设备初始化实现
 * @details 注册 /dev/fb0, /dev/input0, /dev/pwrkey, /dev/ttyS0 到 VFS。
 *          所有设备统一使用 kern_device_t 模型，通过 kern_device_register() 注册。
 *
 * @copyright Copyright (c) 2026
 */
#include "kern_devices.h"
#include "../kern_init.h"
#include "../kern_device.h"

#include "dev_fb0.h"
#include "dev_input0.h"
#include "dev_pwrkey.h"
#include "dev_ttyS0.h"

kern_err_t kern_devices_init(void)
{
    kern_err_t rc;

    rc = kern_device_register(&g_fb0_dev);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/fb0: %d", rc);
        return rc;
    }

    rc = kern_device_register(&g_input0_dev);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/input0: %d", rc);
        return rc;
    }

    rc = kern_device_register(&g_ttyS0_dev);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/ttyS0: %d", rc);
        return rc;
    }

    rc = kern_device_register(&g_pwrkey_dev);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/pwrkey: %d", rc);
        return rc;
    }

    kern_log(KERN_LOG_INFO, "physical devices registered");
    return KERN_OK;
}
