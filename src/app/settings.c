#include "settings.h"

#include "storage.h"

int16_t g_brightness_level       = 5;
int16_t g_anim_speed_level       = 5;
extern bool g_anim_enabled;
int16_t g_screen_rotation_level  = ORIENTATION_LANDSCAPE;

/* 屏幕旋转兼容映射（声明在 rotation_helper.c） */
extern int16_t resolve_rotation_level(uint8_t saved_rot);

void settings_load_from_storage(void)
{
    /* 亮度等级（1-10） */
    int16_t saved_bright = storage_get_brightness();
    if (saved_bright >= 0) {
        if (saved_bright >= 1 && saved_bright <= 10) {
            g_brightness_level = saved_bright;
        } else {
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
        g_anim_speed_level = (saved_anim - 40) / 5;
        if (g_anim_speed_level < 1) g_anim_speed_level = 1;
        if (g_anim_speed_level > 10) g_anim_speed_level = 10;
    }

    /* 动画开关 */
    g_anim_enabled = storage_get_anim_enabled();

    /* 屏幕方向等级 */
    uint8_t saved_rot = storage_get_screen_rotation();
    g_screen_rotation_level = resolve_rotation_level(saved_rot);
}

int16_t settings_brightness_hw_value(void)
{
    int16_t brightness = g_brightness_level * 10;
    return (int16_t)((brightness * 255) / 100);
}

int16_t settings_anim_speed_value(void)
{
    return 40 + g_anim_speed_level * 5;
}
