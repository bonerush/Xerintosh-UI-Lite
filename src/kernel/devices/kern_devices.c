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

static kern_device_t *s_devices[] = {
    &g_fb0_dev,
    &g_input0_dev,
    &g_ttyS0_dev,
    &g_pwrkey_dev,
};
static const size_t s_device_count = sizeof(s_devices) / sizeof(s_devices[0]);

kern_err_t kern_devices_init(void)
{
    kern_device_init();

    size_t registered = 0;
    for (size_t i = 0; i < s_device_count; i++) {
        kern_err_t rc = kern_device_register(s_devices[i]);
        if (rc != KERN_OK) {
            kern_log(KERN_LOG_ERROR,
                     "failed to register /dev/%s: %d",
                     s_devices[i]->name, rc);
            while (registered > 0) {
                registered--;
                kern_device_unregister(s_devices[registered]);
            }
            return rc;
        }
        registered++;
    }

    kern_log(KERN_LOG_INFO, "physical devices registered");
    return KERN_OK;
}
