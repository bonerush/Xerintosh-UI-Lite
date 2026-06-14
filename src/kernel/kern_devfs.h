/**
 * @file   kern_devfs.h
 * @brief  Xeros 设备文件系统（devfs）头文件
 * @details 提供 devfs 初始化接口，负责创建 /dev 目录。
 *          设备注册统一通过 kern_device_register() 完成。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_DEVFS_H
#define KERN_DEVFS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ devfs 生命周期 ═══ */

/**
 * @brief 初始化设备文件系统
 * @note  创建 /dev 目录，初始化 devfs 内部数据结构
 */
extern void kern_devfs_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_DEVFS_H */
