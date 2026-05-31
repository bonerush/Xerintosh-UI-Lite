/**
 * @file   dev_pwrkey.h
 * @brief  /dev/pwrkey 电源键设备头文件
 * @details 将 HAL 电源键事件映射为 VFS 文件操作。
 *          read() 返回结构化电源键事件（9 字节）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef DEV_PWRKEY_H
#define DEV_PWRKEY_H

#include "../kern_types.h"
#include "../kern_vfs.h"

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

/* ═══ 设备操作表 ═══ */

/**
 * @brief  获取 /dev/pwrkey 的文件操作表
 * @return 文件操作表指针（静态分配，始终有效）
 */
extern kern_file_ops_t *dev_pwrkey_get_fops(void);

#ifdef __cplusplus
}
#endif

#endif /* DEV_PWRKEY_H */
