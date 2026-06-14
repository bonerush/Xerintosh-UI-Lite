/**
 * @file   settings.c
 * @brief  系统设置模块实现
 * @details 管理亮度、动画速度、动画开关、屏幕方向四项设置。
 *          负责从 NVS 存储加载设置，并处理新旧存储格式的兼容性转换。
 *
 * @copyright Copyright (c) 2026
 */

#include "settings.h"

#include "app/storage/storage.h"

/* ═══ 全局状态定义 ═══ */

int16_t g_brightness_level       = 5;                        /* 默认亮度等级 5 */
int16_t g_anim_speed_level       = 5;                        /* 默认动画速度 5 */
int16_t g_screen_rotation_level  = ORIENTATION_LANDSCAPE;    /* 默认横屏 */
bool    g_is_landscape           = true;                     /* 默认横屏 */
int16_t g_serial_baud_rate       = 5;                        /* 默认波特率等级 5 = 115200 */

/* ═══ 从存储加载 ═══ */

/**
 * @brief 从 NVS 存储加载所有设置项到全局变量
 * @note  处理新旧存储格式的兼容性转换：
 *        - 亮度：旧格式直接存储 1-10；异常值做范围裁剪
 *        - 动画速度：旧格式存储 40-95（步进 5），新格式存储 1-10
 *        - 屏幕方向：新 key 直接存储 1/2，无效值默认横屏
 */
void settings_load_from_storage(void)
{
    /* 亮度等级（1-10） */
    int16_t saved_bright = storage_get_brightness();
    if (saved_bright >= 0) {
        if (saved_bright >= 1 && saved_bright <= 10) {
            g_brightness_level = saved_bright;
        } else {
            /* 异常旧值：向上取整到最近的等级并裁剪 */
            g_brightness_level = (saved_bright + 9) / 10;
            if (g_brightness_level < 1) g_brightness_level = 1;
            if (g_brightness_level > 10) g_brightness_level = 10;
        }
    }

    /* 动画速度等级（1-10） */
    uint8_t saved_anim = storage_get_anim_speed();
    if (saved_anim >= 1 && saved_anim <= 10) {
        g_anim_speed_level = saved_anim;
    } else if (saved_anim >= 40 && saved_anim <= 95) {
        /* 旧格式转换：40-95 步进 5 映射到 1-10 */
        g_anim_speed_level = (saved_anim - 40) / 5;
        if (g_anim_speed_level < 1) g_anim_speed_level = 1;
        if (g_anim_speed_level > 10) g_anim_speed_level = 10;
    }

    /* 动画开关 */
    g_anim_enabled = storage_get_anim_enabled();

    /* 屏幕方向等级（新 key screen_orient 直接存储新格式值 1/2） */
    uint8_t saved_rot = storage_get_screen_rotation();
    if (saved_rot == ORIENTATION_PORTRAIT || saved_rot == ORIENTATION_LANDSCAPE) {
        g_screen_rotation_level = saved_rot;
    } else {
        g_screen_rotation_level = ORIENTATION_LANDSCAPE;
    }

    /* 同步到 bool 开关 */
    g_is_landscape = (g_screen_rotation_level == ORIENTATION_LANDSCAPE);

    /* 波特率等级（1-6） */
    int16_t saved_baud = storage_get_serial_baud_rate();
    if (saved_baud >= 1 && saved_baud <= 6) {
        g_serial_baud_rate = saved_baud;
    } else {
        g_serial_baud_rate = 5; /* 默认 115200 */
    }
}

/* ═══ Getter/Setter ═══ */

int16_t settings_get_brightness(void) { return g_brightness_level; }
void settings_set_brightness(int16_t level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    g_brightness_level = level;
}

int16_t settings_get_anim_speed(void) { return g_anim_speed_level; }
void settings_set_anim_speed(int16_t level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    g_anim_speed_level = level;
}

int16_t settings_get_rotation(void) { return g_screen_rotation_level; }
void settings_set_rotation(int16_t level) {
    g_screen_rotation_level = level;
    g_is_landscape = (level == ORIENTATION_LANDSCAPE);
}

bool settings_get_landscape(void) { return g_is_landscape; }
void settings_set_landscape(bool landscape) {
    g_is_landscape = landscape;
    g_screen_rotation_level = landscape ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT;
}

int16_t settings_get_baud_rate(void) { return g_serial_baud_rate; }
void settings_set_baud_rate(int16_t level) {
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    g_serial_baud_rate = level;
}

/* ═══ 值转换 ═══ */

/**
 * @brief 将亮度等级转换为硬件 PWM 值（0-255）
 * @return 硬件亮度值
 */
int16_t settings_brightness_hw_value(void)
{
    int16_t brightness = g_brightness_level * 10;
    return (int16_t)((brightness * 255) / 100);
}

/**
 * @brief 将动画速度等级转换为内部动画速度值
 * @return 内部动画速度值（40 + level * 5）
 */
int16_t settings_anim_speed_value(void)
{
    return 40 + g_anim_speed_level * 5;
}

/* ═══ 波特率映射表 ═══ */

static const int32_t s_baud_rate_table[] = {
    9600,    /* level 1 */
    19200,   /* level 2 */
    38400,   /* level 3 */
    57600,   /* level 4 */
    115200,  /* level 5 */
    230400   /* level 6 */
};
#define BAUD_RATE_TABLE_SIZE (sizeof(s_baud_rate_table) / sizeof(s_baud_rate_table[0]))

/**
 * @brief  将波特率等级转换为实际波特率值
 * @param  level 等级 1-6
 * @return 实际波特率值
 * @note   无效低值回退到默认值 115200，无效高值回退到最大值 230400
 */
int32_t settings_serial_baud_hw_value(int16_t level)
{
    if (level < 1) {
        return 115200; /* 默认值 */
    }
    if (level > (int16_t)BAUD_RATE_TABLE_SIZE) {
        return s_baud_rate_table[BAUD_RATE_TABLE_SIZE - 1]; /* 最大值 */
    }
    return s_baud_rate_table[level - 1];
}

