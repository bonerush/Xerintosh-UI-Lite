/**
 * @file   about.c
 * @brief  关于页面 App 实现
 * @details 显示内核版本、系统名称、logo 及开发者信息。
 *          纯黑白配色，参照 serial_monitor 的 user_item 模式。
 *
 * @copyright Copyright (c) 2026
 */

#include "about.h"
#include "kernel/kern_version.h"
#include "app/boot/boot_screen.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include <stdio.h>
#include <stddef.h>

/* ═══ 前向声明 ═══ */

static void about_draw(void);

/* ═══ 渲染 ═══ */

static void about_draw(void)
{
    int16_t fh = hal_get_font_height();
    int16_t sw = SCREEN_WIDTH;
    int16_t sh = SCREEN_HEIGHT;

    /*
     * 自适应布局：
     * - 竖屏（80x160）：logo 在上，信息在下，充裕间距
     * - 横屏（160x80）：logo 缩小，信息紧凑排列
     */
    int16_t logo_scale;
    int16_t logo_ox, logo_oy;
    int16_t title_y;
    int16_t info_start_y;
    int16_t line_dy;

    if (sh >= 140) {
        /* 竖屏：充裕布局 */
        logo_scale  = 80;
        logo_ox     = (sw - 44 * logo_scale / 100) / 2;
        logo_oy     = 2;
        title_y     = logo_oy + 60 * logo_scale / 100 + 5;
        info_start_y = title_y + fh + 6;
        line_dy     = fh + 5;
    } else {
        /* 横屏：紧凑布局 */
        logo_scale  = 55;
        logo_ox     = 4;
        logo_oy     = 2;
        title_y     = logo_oy + 60 * logo_scale / 100 + 4;
        info_start_y = title_y + fh + 2;
        line_dy     = fh + 1;
    }

    /* Logo */
    boot_screen_draw_logo(logo_ox, logo_oy, logo_scale);

    /* 系统名称 */
    hal_set_font(NULL);
    int16_t tw = hal_get_string_width("Xerintosh");
    hal_draw_string((sw - tw) / 2, title_y, "Xerintosh", COLOR_FG);

    /* 信息区 */
    hal_set_font(hal_get_cn_font());
    int16_t lx = 4;
    int16_t y  = info_start_y;
    char buf[64];

    /* 分隔线 */
    hal_draw_line(lx, y, sw - 8, y, COLOR_FG);
    y += 4;

    snprintf(buf, sizeof(buf), "v" XEROS_VERSION_STRING "  " XEROS_CODENAME);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += line_dy;

    snprintf(buf, sizeof(buf), XEROS_PLATFORM);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += line_dy;

    snprintf(buf, sizeof(buf), "by " XEROS_DEVELOPER);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += line_dy;

    snprintf(buf, sizeof(buf), __DATE__);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);

    /* 底部提示 */
    hal_set_font(hal_get_cn_font());
    hal_draw_string(lx, sh - 2, "Hold BtnB to return", COLOR_FG);
}

/* ═══ 生命周期 ═══ */

void about_init(void)
{
#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif
}

void about_loop(void)
{
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_LONG_PRESS) {
        xerintosh_user_item_t* current =
            xerintosh_to_user_item(g_xerintosh_selector.selected_item);
        if (current != NULL && !current->exiting_user_item) {
            xerintosh_selector_exit_current_item();
        }
        return;
    }

    about_draw();
}

void about_exit(void)
{
#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif
}
