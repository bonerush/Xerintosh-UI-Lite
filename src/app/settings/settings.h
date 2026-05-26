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
extern bool    g_anim_enabled;           /* 动画开关 */
extern int16_t g_screen_rotation_level;  /* 屏幕方向 1=竖屏, 2=横屏 */
extern bool    g_is_landscape;           /* 横屏开关：true=横屏, false=竖屏 */
extern int16_t g_serial_baud_rate;       /* 串口波特率等级 1-6 */

/* ═══ 生命周期 ═══ */

/**
 * @brief 从 NVS 存储加载所有设置项到全局变量
 */
void settings_load_from_storage(void);

/* ═══ 值转换 ═══ */

/**
 * @brief  将亮度等级转换为硬件 PWM 值（0-255）
 * @return 硬件亮度值
 */
int16_t settings_brightness_hw_value(void);

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

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
