/**
 * @file   kern_devices.h
 * @brief  物理设备初始化头文件
 * @details 注册 /dev/fb0, /dev/input0, /dev/ttyS0 到 VFS。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_DEVICES_H
#define KERN_DEVICES_H

#include "../kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册所有物理设备到 VFS
 * @note  依赖 devfs 已初始化（kern_devfs_init）
 * @return KERN_OK 成功，< 0 为错误码
 */
extern int kern_devices_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_DEVICES_H */
