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
#include "../kern_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 输入事件结构体 ═══ */

#define INPUT_EVENT_SIZE  6  /* button(1) + event(1) + timestamp(4) */

/**
 * @brief 按键事件（read() 返回此结构体的原始字节）
 */
typedef struct {
    uint8_t  button;     /* hal_button_t: 0=BtnA, 1=BtnB */
    uint8_t  event;      /* hal_event_t: 0=None, 1=Short, 2=Long, 3=Double */
    uint32_t timestamp;  /* hal_get_ticks() 时间戳 */
} input_event_t;

/* ═══ ioctl 命令 ═══ */

#define INPUT_IOCTL_SET_DOUBLE_CLICK  0x20  /* arg = 0 (disable) / 1 (enable) */

/* ═══ 设备操作表 ═══ */

/**
 * @brief  获取 /dev/input0 的文件操作表
 * @return 文件操作表指针（静态分配，始终有效）
 */
extern kern_file_ops_t *dev_input0_get_fops(void);

#ifdef __cplusplus
}
#endif

#endif /* DEV_INPUT0_H */
