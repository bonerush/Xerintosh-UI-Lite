/**
 * @file   kern_sysfs.h
 * @brief  Xeros sysfs —— /sys 虚拟文件系统头文件
 * @details 提供系统配置参数的虚拟文件访问接口（brightness、rotation、
 *          anim_speed、anim_enabled、log_level）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SYSFS_H
#define KERN_SYSFS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 sysfs 子系统
 * @note  创建 /sys 和 /sys/kernel 目录，注册所有 sysfs 文件
 */
extern void kern_sysfs_init(void);

/* ═══ Getter/Setter ═══ */

extern int32_t kern_sysfs_get_brightness(void);
extern void kern_sysfs_set_brightness(int32_t val);

extern int32_t kern_sysfs_get_rotation(void);
extern void kern_sysfs_set_rotation(int32_t val);

extern int32_t kern_sysfs_get_anim_speed(void);
extern void kern_sysfs_set_anim_speed(int32_t val);

extern int32_t kern_sysfs_get_anim_enabled(void);
extern void kern_sysfs_set_anim_enabled(int32_t val);

extern int32_t kern_sysfs_get_log_level(void);
extern void kern_sysfs_set_log_level(int32_t val);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SYSFS_H */
