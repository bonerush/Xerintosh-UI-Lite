/**
 * @file   dev_fb0.h
 * @brief  /dev/fb0 帧缓冲设备头文件
 * @details 将 HAL 显示层绘制原语映射为 VFS 文件操作。
 *          write() 接受二进制命令协议，ioctl() 用于显示配置。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef DEV_FB0_H
#define DEV_FB0_H

#include "../kern_types.h"
#include "../kern_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 写命令协议 ═══ */

#define DEV_FB_CMD_PIXEL      0x01  /* 绘点:   + int16 x, int16 y, uint16 color (7B) */
#define DEV_FB_CMD_FILL_RECT  0x02  /* 填充矩形: + int16 x, int16 y, int16 w, int16 h, uint16 color (11B) */
#define DEV_FB_CMD_CLEAR      0x03  /* 清屏:   + uint16 color (3B) */
#define DEV_FB_CMD_FLUSH      0x04  /* 刷新:   无附加数据 (1B) */

/* ═══ ioctl 命令 ═══ */

#define DEV_FB_IOCTL_SET_ROTATION  0x10  /* arg = 0-3 */
#define DEV_FB_IOCTL_GET_WIDTH     0x11  /* 返回屏幕宽度 */
#define DEV_FB_IOCTL_GET_HEIGHT    0x12  /* 返回屏幕高度 */

/* ═══ 设备描述符 ═══ */

/**
 * @brief /dev/fb0 设备实例（统一设备模型）
 * @note  通过 kern_device_register(&g_fb0_dev) 注册
 */
extern kern_device_t g_fb0_dev;

#ifdef __cplusplus
}
#endif

#endif /* DEV_FB0_H */
