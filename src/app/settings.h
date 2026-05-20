#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设置状态变量 */
extern int16_t g_brightness_level;
extern int16_t g_anim_speed_level;
extern bool    g_anim_enabled;
extern int16_t g_screen_rotation_level;

/* 从存储加载所有设置 */
void settings_load_from_storage(void);

/* 将等级值转换为实际硬件值 */
int16_t settings_brightness_hw_value(void);
int16_t settings_anim_speed_value(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
