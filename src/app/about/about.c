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
#include "hal/hal_layout.h"
#include "hal/hal_input.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include <stdio.h>

/* ═══ 布局常量 ═══ */

#define ABOUT_LOGO_SCALE   80
#define ABOUT_LEFT_MARGIN  HAL_LEFT_X()       /* 标准左缩进 = HAL_MARGIN_MD(4) */
#define ABOUT_SEP_GAP      10                 /* 分隔线间距 */
#define ABOUT_INFO_GAP     17                 /* 信息区间距 */
#define ABOUT_TITLE_GAP    4                  /* 标题与 logo 间距 */

/* ═══ 渲染 ═══ */

static void about_draw(void)
{
    int16_t fh = hal_get_font_height();

    /* Logo 像素尺寸（必须与 boot_screen_draw_logo 内部公式一致） */
    int16_t logo_w = 44 * ABOUT_LOGO_SCALE / 100;
    int16_t logo_h = 60 * ABOUT_LOGO_SCALE / 100;

    /* 标题宽度 */
    hal_set_font(NULL);
    int16_t title_w = hal_get_string_width("Xerintosh");

    /*
     * 左右对齐：较宽者左对齐到 ABOUT_LEFT_MARGIN，
     * 较窄者居中于较宽者下方。
     */
    int16_t content_w = (logo_w > title_w) ? logo_w : title_w;
    int16_t logo_x, title_x;
    if (logo_w > title_w) {
        logo_x  = ABOUT_LEFT_MARGIN;
        title_x = ABOUT_LEFT_MARGIN + (logo_w - title_w) / 2;
    } else {
        title_x = ABOUT_LEFT_MARGIN;
        logo_x  = ABOUT_LEFT_MARGIN + (title_w - logo_w) / 2;
    }

    /* 垂直居中：logo + 标题作为一个整体 */
    int16_t group_h = logo_h + fh + ABOUT_TITLE_GAP;
    int16_t y0 = HAL_CENTER_Y(group_h);

    boot_screen_draw_logo(logo_x, y0, ABOUT_LOGO_SCALE);
    hal_draw_string(title_x, y0 + group_h, "Xerintosh", COLOR_FG);

    /* 分隔线（纵贯屏幕） */
    hal_draw_v_line(ABOUT_LEFT_MARGIN + content_w + ABOUT_SEP_GAP,
                    y0, SCREEN_HEIGHT - 2 * y0, COLOR_FG);

    /* 信息区 */
    hal_set_font(hal_get_cn_font());
    int16_t info_x = ABOUT_LEFT_MARGIN + content_w + ABOUT_INFO_GAP;
    char buf[32];

    snprintf(buf, sizeof(buf), "Version:%s", XEROS_VERSION_STRING);
    hal_draw_string(info_x, y0 + fh,              buf,              COLOR_FG);
    hal_draw_string(info_x, y0 + fh + fh,         XEROS_CODENAME,   COLOR_FG);
    hal_draw_string(info_x, y0 + fh + 2 * fh,     XEROS_PLATFORM,   COLOR_FG);

    snprintf(buf, sizeof(buf), "By:%s", XEROS_DEVELOPER);
    hal_draw_string(info_x, y0 + fh + 4 * fh,     buf,              COLOR_FG);
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
