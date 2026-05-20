#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 屏幕方向枚举（直观可读） ─── */

typedef enum {
    ORIENTATION_PORTRAIT  = 1,  /* 竖屏：rotation = 0°  */
    ORIENTATION_LANDSCAPE = 2,  /* 横屏：rotation = 90° */
} screen_orientation_t;

/* ─── 全局状态 ─── */

extern int16_t g_brightness_level;
extern int16_t g_anim_speed_level;
extern bool    g_anim_enabled;
extern int16_t g_screen_rotation_level;

/* ─── 生命周期 ─── */

void settings_load_from_storage(void);

/* ─── 值转换 ─── */

int16_t settings_brightness_hw_value(void);
int16_t settings_anim_speed_value(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
