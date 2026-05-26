/**
 * @file   dev_ttyS0.h
 * @brief  /dev/ttyS0 串口设备头文件
 * @details 将硬件串口映射为 VFS 文件操作。
 *          read() 从串口接收缓冲区读取，write() 向串口发送数据。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef DEV_TTYS0_H
#define DEV_TTYS0_H

#include "../kern_types.h"
#include "../kern_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 设备操作表 ═══ */

/**
 * @brief  获取 /dev/ttyS0 的文件操作表
 * @return 文件操作表指针（静态分配，始终有效）
 */
extern kern_file_ops_t *dev_ttyS0_get_fops(void);

#ifdef __cplusplus
}
#endif

#endif /* DEV_TTYS0_H */
