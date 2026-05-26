/**
 * @file   kern_devfs.h
 * @brief  Xeros 设备文件系统（devfs）头文件
 * @details 提供设备注册接口，将物理/虚拟设备挂载到 VFS 路径树上。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_DEVFS_H
#define KERN_DEVFS_H

#include "kern_types.h"
#include "kern_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ devfs 生命周期 ═══ */

/**
 * @brief 初始化设备文件系统
 * @note  创建 /dev 目录，初始化 devfs 内部数据结构
 */
extern void kern_devfs_init(void);

/* ═══ 设备注册 ═══ */

/**
 * @brief  注册一个设备到 /dev/ 下
 * @param  name 设备名称（如 "null", "ttyS0", "fb0"）
 * @param  fops 设备操作函数表
 * @param  type 设备类型（通常为 KERN_FILE_CHRDEV）
 * @param  private_data 设备私有数据（可为 NULL）
 * @return KERN_OK 成功，< 0 为错误码
 * @note   自动拼接路径为 "/dev/<name>"
 */
extern int kern_dev_register(const char *name, kern_file_ops_t *fops,
                             kern_file_type_t type, void *private_data);

#ifdef __cplusplus
}
#endif

#endif /* KERN_DEVFS_H */
