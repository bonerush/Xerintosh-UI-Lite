/**
 * @file   kern_gpiofs.h
 * @brief  Xeros GPIO 虚拟文件系统头文件
 * @details 定义 kern_gpiofs_init() 接口，将 M5Stick-C 的 GPIO 引脚状态
 *          以文件形式挂载到 /sys/gpio/ 路径下，支持读取和输出引脚写入。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_GPIOFS_H
#define KERN_GPIOFS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ gpiofs 生命周期 ═══ */

/**
 * @brief 初始化 /sys/gpio 虚拟文件系统
 * @note  创建 /sys/gpio 目录，注册各引脚文件。
 *        需在 VFS 和 sysfs 初始化之后调用。
 */
extern kern_err_t kern_gpiofs_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_GPIOFS_H */
