/**
 * @file   dev_input0.h
 * @brief  /dev/input0 按键输入设备头文件
 * @details 将 HAL 输入层事件映射为 VFS 文件操作。
 *          read() 返回结构化按键事件，ioctl() 用于输入配置。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef DEV_INPUT0_H
#define DEV_INPUT0_H

#include "../kern_types.h"
#include "../kern_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 输入事件结构体 ═══ */

#define DEV_INPUT_EVENT_SIZE  6  /* button(1) + event(1) + timestamp(4) */

/**
 * @brief 按键事件（read() 返回此结构体的原始字节）
 */
typedef struct {
    uint8_t  button;     /* hal_button_t: 0=BtnA, 1=BtnB */
    uint8_t  event;      /* hal_event_t: 0=None, 1=Short, 2=Long, 3=Double */
    uint32_t timestamp;  /* hal_get_ticks() 时间戳 */
} dev_input_event_t;

/* ═══ ioctl 命令 ═══ */

#define DEV_INPUT_IOCTL_SET_DOUBLE_CLICK  0x20  /* arg = 0 (disable) / 1 (enable) */

/* ═══ 设备描述符 ═══ */

/**
 * @brief /dev/input0 设备实例（统一设备模型）
 * @note  通过 kern_device_register(&g_input0_dev) 注册
 */
extern kern_device_t g_input0_dev;

#ifdef __cplusplus
}
#endif

#endif /* DEV_INPUT0_H */
