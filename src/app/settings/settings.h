/**
 * @file   settings.h
 * @brief  系统设置模块头文件
 * @details 定义屏幕方向枚举、全局设置变量及从 NVS 存储加载设置的接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/* UI 上下文（提供 g_anim_enabled 等全局状态宏） */
#include "ui/ui_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 屏幕方向枚举 ═══ */

/**
 * @brief 屏幕方向枚举
 */
typedef enum {
    ORIENTATION_PORTRAIT  = 1,  /* 竖屏：rotation = 0°  */
    ORIENTATION_LANDSCAPE = 2,  /* 横屏：rotation = 90° */
} screen_orientation_t;

/* ═══ 全局状态 ═══ */

extern int16_t g_brightness_level;       /* 亮度等级 1-10 */
extern int16_t g_anim_speed_level;       /* 动画速度等级 1-10 */
extern int16_t g_screen_rotation_level;  /* 屏幕方向 1=竖屏, 2=横屏 */
extern bool    g_is_landscape;           /* 横屏开关：true=横屏, false=竖屏 */
extern int16_t g_serial_baud_rate;       /* 串口波特率等级 1-6 */
extern int16_t g_spring_stiffness_level;  /* 弹簧硬度等级 1-10（默认 5） */
extern int16_t g_spring_damping_level;    /* 弹簧阻尼等级 1-10（默认 9） */

/* ═══ Getter/Setter ═══ */

extern int16_t settings_get_brightness(void);
extern void    settings_set_brightness(int16_t level);

extern int16_t settings_get_anim_speed(void);
extern void    settings_set_anim_speed(int16_t level);

extern int16_t settings_get_rotation(void);
extern void    settings_set_rotation(int16_t level);

extern bool    settings_get_landscape(void);
extern void    settings_set_landscape(bool landscape);

extern int16_t settings_get_baud_rate(void);
extern void    settings_set_baud_rate(int16_t level);

extern int16_t settings_get_spring_stiffness(void);
extern void    settings_set_spring_stiffness(int16_t level);

extern int16_t settings_get_spring_damping(void);
extern void    settings_set_spring_damping(int16_t level);

/* ═══ 生命周期 ═══ */

/**
 * @brief 从 NVS 存储加载所有设置项到全局变量
 */
void settings_load_from_storage(void);

/* ═══ 统一转换入口 ═══ */

/**
 * @brief 设置项类型标识
 */
typedef enum {
    SETTINGS_KIND_BRIGHTNESS,        /**< 亮度 1-10 → 0-255 */
    SETTINGS_KIND_ANIM_SPEED,        /**< 动画速度 1-10 → 40-95 */
    SETTINGS_KIND_SPRING_STIFFNESS,  /**< 弹簧硬度 1-10 → 0.04-0.40 */
    SETTINGS_KIND_SPRING_DAMPING,    /**< 弹簧阻尼 1-10 → 0.04-0.40 */
    SETTINGS_KIND_BAUD_RATE,         /**< 波特率 1-6 → 9600-230400 */
} settings_kind_t;

/**
 * @brief  将设置等级转换为硬件/内部值
 * @param  kind  设置项类型
 * @param  level 等级值
 * @return 硬件值（亮度/PWM 用低 16 位；浮点参数放大 10000 倍存储）
 * @note   弹簧参数返回 level * 10000 * 0.04 的整数值，调用方可除以 10000.0f 得浮点
 */
int32_t settings_level_to_hw(settings_kind_t kind, int16_t level);

/**
 * @brief  将硬件/内部值反向映射为设置等级
 * @param  kind 设置项类型
 * @param  hw   硬件值
 * @return 等级值
 */
int16_t settings_hw_to_level(settings_kind_t kind, int32_t hw);

/* ═══ 值转换 ═══ */

/**
 * @brief  将亮度等级转换为硬件 PWM 值（0-255）
 * @return 硬件亮度值
 */
int16_t settings_brightness_hw_value(void);

/**
 * @brief  将硬件 PWM 值（0-255）反向映射为亮度等级（1-10）
 * @param  hw 硬件亮度值（0-255）
 * @return 亮度等级（1-10）
 */
int16_t settings_brightness_level_from_hw(int16_t hw);

/**
 * @brief  将动画速度等级转换为内部动画速度值
 * @return 内部动画速度值
 */
int16_t settings_anim_speed_value(void);

/**
 * @brief  将波特率等级转换为实际波特率值
 * @param  level 等级 1-6
 * @return 实际波特率值（9600, 19200, 38400, 57600, 115200, 230400）
 * @note   无效值回退到默认值 115200（level 5）
 */
int32_t settings_serial_baud_hw_value(int16_t level);

/**
 * @brief  获取波特率映射表项数
 * @return 等级总数（当前为 6）
 */
int settings_serial_baud_count(void);

/**
 * @brief  获取波特率映射表只读指针
 * @return 指向 int32_t 数组首元素的常量指针
 * @note   数组长度可通过 settings_serial_baud_count() 获取
 */
const int32_t *settings_serial_baud_table(void);

/**
 * @brief  将弹簧硬度等级转换为实际 stiffness 浮点值
 * @param  level 等级 1-10
 * @return 浮点刚度值（0.04-0.40）
 */
float settings_spring_stiffness_hw_value(int16_t level);

/**
 * @brief  将弹簧阻尼等级转换为实际 damping 浮点值
 * @param  level 等级 1-10
 * @return 浮点阻尼值（0.04-0.40）
 */
float settings_spring_damping_hw_value(int16_t level);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
