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
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include <stdio.h>
#include <stddef.h>

/* ═══ 前向声明 ═══ */

static void about_draw(void);

/* ═══ logo 绘制 ═══ */

/**
 * @brief 绘制 Xerintosh 系统 logo（像素艺术 'X' 标记）
 * @note  基于 M5Stick-C 80x160 屏幕设计，左上角定位
 */
static void draw_logo(int16_t ox, int16_t oy)
{
    /* 大 X 标记：4px 粗的交叉线 */
    for (int i = 0; i < 4; i++) {
        /* 左上 → 右下 */
        hal_draw_line(ox + 10, oy + i, ox + 34, oy + 28 + i, COLOR_FG);
        /* 右上 → 左下 */
        hal_draw_line(ox + 34, oy + i, ox + 10, oy + 28 + i, COLOR_FG);
    }

    /* 外框 */
    hal_draw_rect(ox, oy, 44, 32, COLOR_FG);
}

/* ═══ 信息绘制 ═══ */

static void draw_info(void)
{
    int16_t fh  = hal_get_font_height();
    int16_t lx  = 4;            /* 左边距 */
    int16_t y   = 42;           /* 信息区起始 Y */
    int16_t dy  = fh + 6;       /* 行距 */

    char buf[64];

    hal_set_font(hal_get_cn_font());

    /* 系统名称 */
    snprintf(buf, sizeof(buf), "Xerintosh UI");
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy + 4;

    /* 分隔线 */
    hal_draw_line(lx, y, SCREEN_WIDTH - 8, y, COLOR_FG);
    y += 4;

    /* 版本 */
    snprintf(buf, sizeof(buf), "Version: " XEROS_VERSION_STRING);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    /* 代号 */
    snprintf(buf, sizeof(buf), "Codename: " XEROS_CODENAME);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    /* 平台 */
    snprintf(buf, sizeof(buf), "Platform: " XEROS_PLATFORM);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    /* 开发者 */
    snprintf(buf, sizeof(buf), "Developer: " XEROS_DEVELOPER);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy;

    /* 编译日期 */
    snprintf(buf, sizeof(buf), "Built: " __DATE__ " " __TIME__);
    hal_draw_string(lx, y + fh, buf, COLOR_FG);
    y += dy + 8;

    /* 底部提示 */
    hal_draw_string(lx, SCREEN_HEIGHT - fh - 4 + fh,
                    "Hold BtnB to return", COLOR_FG);
}

/* ═══ 渲染入口 ═══ */

static void about_draw(void)
{
    draw_logo(18, 5);
    draw_info();
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
