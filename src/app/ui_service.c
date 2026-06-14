/**
 * @file   ui_service.c
 * @brief  App 层 UI 公共服务实现
 * @details 统一封装 user_item 生命周期中的公共操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_service.h"

#include "app/settings/settings.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_screen.h"
#include "ui/ui_item.h"

/* 进入全屏 App 前保存的屏幕方向 */
static bool s_prev_landscape = true;

void ui_service_user_item_init(void)
{
    hal_input_reset_events();
}

bool ui_service_user_item_loop(hal_event_t event_b)
{
    return ui_user_item_try_exit(event_b);
}

void ui_service_user_item_exit(void)
{
    hal_input_reset_events();
}

void ui_service_enter_landscape(void)
{
    s_prev_landscape = g_is_landscape;
    if (!g_is_landscape) {
        g_is_landscape = true;
        g_screen_rotation_level = ORIENTATION_LANDSCAPE;
        hal_display_set_rotation(1);
#ifndef NATIVE_TEST
        hal_screen_get_size(&g_screen_width, &g_screen_height);
#else
        {
            int16_t w, h;
            hal_screen_get_size(&w, &h);
        }
#endif
        hal_display_init();
    }
}

void ui_service_exit_landscape(void)
{
    if (!s_prev_landscape) {
        g_is_landscape = false;
        g_screen_rotation_level = ORIENTATION_PORTRAIT;
        hal_display_set_rotation(0);
#ifndef NATIVE_TEST
        hal_screen_get_size(&g_screen_width, &g_screen_height);
#else
        {
            int16_t w, h;
            hal_screen_get_size(&w, &h);
        }
#endif
        hal_display_init();
    }
}
