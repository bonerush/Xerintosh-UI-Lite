/**
 * @file   kern_devices.c
 * @brief  物理设备初始化实现
 * @details 注册 /dev/fb0, /dev/input0, /dev/ttyS0 到 VFS。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_devices.h"
#include "../kern_devfs.h"
#include "../kern_init.h"

#include "dev_fb0.h"
#include "dev_input0.h"
#include "dev_ttyS0.h"

int kern_devices_init(void)
{
    int rc;

    rc = kern_dev_register("fb0", dev_fb0_get_fops(),
                            KERN_FILE_CHRDEV, NULL);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/fb0: %d", rc);
        return rc;
    }

    rc = kern_dev_register("input0", dev_input0_get_fops(),
                            KERN_FILE_CHRDEV, NULL);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/input0: %d", rc);
        return rc;
    }

    rc = kern_dev_register("ttyS0", dev_ttyS0_get_fops(),
                            KERN_FILE_CHRDEV, NULL);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/ttyS0: %d", rc);
        return rc;
    }

    kern_log(KERN_LOG_INFO, "physical devices registered");
    return KERN_OK;
}
