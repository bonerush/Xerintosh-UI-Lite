/**
 * @file   dev_ttyS0.h
 * @brief  /dev/ttyS0 串口设备头文件（统一设备模型）
 * @details 将硬件串口映射为 VFS 文件操作。
 *          read() 从串口接收缓冲区读取，write() 向串口发送数据。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef DEV_TTYS0_H
#define DEV_TTYS0_H

#include "../kern_types.h"
#include "../kern_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 设备描述符 ═══ */

/**
 * @brief /dev/ttyS0 设备实例（统一设备模型）
 * @note  通过 kern_device_register(&g_ttyS0_dev) 注册
 */
extern kern_device_t g_ttyS0_dev;

/**
 * @brief 设备轮询：将数据从硬件串口传输到环形缓冲区（仅 ESP32）
 * @note  必须在主 loop() 中调用，线程安全（单核操作）。
 */
extern void dev_ttyS0_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* DEV_TTYS0_H */
