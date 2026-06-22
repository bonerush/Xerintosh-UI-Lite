/**
 * @file   dev_pwrkey.h
 * @brief  /dev/pwrkey 电源键设备头文件
 * @details 使用统一设备模型（kern_device_t）注册，
 *          read() 返回结构化电源键事件（9 字节）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef DEV_PWRKEY_H
#define DEV_PWRKEY_H

#include "../kern_types.h"
#include "../kern_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 电源键事件结构体 ═══ */

#define DEV_PWRKEY_EVENT_SIZE  9  /* event(1) + hold_ms(4) + timestamp(4) */

/**
 * @brief 电源键事件（read() 返回此结构体的原始字节）
 */
typedef struct {
    uint8_t  event;      /* hal_pwr_key_event_t: 0=None, 1=Short, 2=Long, 3=Hold */
    uint32_t hold_ms;    /* 持续按住时长 */
    uint32_t timestamp;  /* hal_get_ticks() 时间戳 */
} dev_pwrkey_event_t;

/* ═══ 设备描述符 ═══ */

/**
 * @brief /dev/pwrkey 设备实例（统一设备模型）
 * @note  通过 kern_device_register(&g_pwrkey_dev) 注册
 */
extern kern_device_t g_pwrkey_dev;

#ifdef __cplusplus
}
#endif

#endif /* DEV_PWRKEY_H */
