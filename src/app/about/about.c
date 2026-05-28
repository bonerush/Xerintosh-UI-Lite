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

    /* Logo（Xerintosh 设备轮廓，复用于开机画面） */
    boot_screen_draw_logo(18, 4, 80);

    /* 系统名称 */
    hal_set_font(NULL);
    int16_t tw = hal_get_string_width("Xerintosh");
    hal_draw_string((SCREEN_WIDTH - tw) / 2, 54, "Xerintosh", COLOR_FG);

    /* 信息区 */
    hal_set_font(hal_get_cn_font());
    int16_t lx  = 4;
    int16_t y   = 62;
    int16_t dy  = fh + 4;
    char buf[64];

    /* 分隔线 */
    hal_draw_line(lx, y, SCREEN_WIDTH - 8, y, COLOR_FG);
    y += 6;

    snprintf(buf, sizeof(buf), "Version: " XEROS_VERSION_STRING);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    snprintf(buf, sizeof(buf), "Codename: " XEROS_CODENAME);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    snprintf(buf, sizeof(buf), "Platform: " XEROS_PLATFORM);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    snprintf(buf, sizeof(buf), "Developer: " XEROS_DEVELOPER);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    snprintf(buf, sizeof(buf), "Built: " __DATE__ " " __TIME__);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);

    /* 底部提示 */
    hal_set_font(hal_get_cn_font());
    hal_draw_string(4, SCREEN_HEIGHT - fh - 4 + fh,
                    "Hold BtnB to return", COLOR_FG);
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
