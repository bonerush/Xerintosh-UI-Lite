/**
 * @file   kern_sysfs.h
 * @brief  Xeros sysfs — /sys 虚拟文件系统头文件
 * @details 提供系统配置参数的虚拟文件访问接口（brightness、rotation、
 *          anim_speed、anim_enabled、log_level）。
 *
 *          支持双向绑定：sysfs 写入时通过回调通知硬件更新；
 *          硬件变动时通过 kern_sysfs_update() 同步到 sysfs（不触发回调）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SYSFS_H
#define KERN_SYSFS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 属性枚举（公开） ═══ */

typedef enum {
    KERN_SYSFS_BRIGHTNESS   = 0,
    KERN_SYSFS_ROTATION     = 1,
    KERN_SYSFS_ANIM_SPEED   = 2,
    KERN_SYSFS_ANIM_ENABLED = 3,
    KERN_SYSFS_LOG_LEVEL    = 4,
    KERN_SYSFS_MODE         = 5,  /* Phase 3: 运行模式 (0=manual 1=auto 2=calibrate 3=estop) */
    KERN_SYSFS_CTRL         = 6,  /* Phase 3: 控制算法 (0=stop 1=start 2=reset) */
    KERN_SYSFS_ATTR_COUNT   = 7
} kern_sysfs_attr_t;

/* ═══ 变更回调 ═══ */

/**
 * @brief sysfs 属性值变更回调
 * @param attr      被修改的属性 ID
 * @param new_value 新值
 * @param user_data 注册时的用户上下文指针
 */
typedef void (*kern_sysfs_change_callback_t)(kern_sysfs_attr_t attr,
                                            int32_t new_value,
                                            void *user_data);

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 sysfs 子系统
 */
extern void kern_sysfs_init(void);

/* ═══ 绑定接口 ═══ */

/**
 * @brief  注册 sysfs 属性变更回调
 * @param  attr      要监听的属性
 * @param  cb        变更回调函数
 * @param  user_data 回调上下文（可为 NULL）
 * @return KERN_OK 成功，KERN_EINVAL 参数无效
 * @note   回调在 sysfs_write 内部同步调用，需快速返回。
 *         同一属性可绑定多个回调。
 */
extern int kern_sysfs_bind(kern_sysfs_attr_t attr,
                           kern_sysfs_change_callback_t cb,
                           void *user_data);

/**
 * @brief  从外部（如 UI settings）同步值到 sysfs
 * @param  attr  属性 ID
 * @param  value 新值
 * @note   仅更新内部值，不触发变更回调（避免循环触发）。
 *         用于 UI 修改 → sysfs 单向同步。
 */
extern void kern_sysfs_update(kern_sysfs_attr_t attr, int32_t value);

/* ═══ Getter/Setter（直接操作内部值，不触发回调） ═══ */

extern int32_t kern_sysfs_get_brightness(void);
extern void    kern_sysfs_set_brightness(int32_t val);

extern int32_t kern_sysfs_get_rotation(void);
extern void    kern_sysfs_set_rotation(int32_t val);

extern int32_t kern_sysfs_get_anim_speed(void);
extern void    kern_sysfs_set_anim_speed(int32_t val);

extern int32_t kern_sysfs_get_anim_enabled(void);
extern void    kern_sysfs_set_anim_enabled(int32_t val);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SYSFS_H */
