/**
 * @file   flasher_ui.cpp
 * @brief  烧录进度条 UI 实现
 * @details 全屏进度条，支持反色文字、跑马灯动画及成功/失败状态显示。
 *          桥接模式下显示 "BRIDGE"（就绪）/ "FLASHING..."（烧录中）。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher_ui.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 限制进度值在 [0, 100] 范围内
 */
static int flasher_clamp_progress(int pct)
{
    if (pct < 0) return 0;
    if (pct > 100) return 100;
    return pct;
}

/**
 * @brief 根据状态获取文字颜色
 */
static uint16_t flasher_ui_text_color(flasher_ui_status_t status)
{
    switch (status) {
    case FLASHER_UI_SUCCESS:
        return COLOR_ACCENT;
    case FLASHER_UI_FAILED:
        return COLOR_RED;
    case FLASHER_UI_BRIDGE:
    case FLASHER_UI_FLASHING:
    default:
        return COLOR_FG;
    }
}

void flasher_ui_init(flasher_ui_state_t *st)
{
    if (!st) return;
    st->status = FLASHER_UI_BRIDGE;
    st->progress = 0;
    st->start_ms = hal_get_ticks();
}

void flasher_ui_set_progress(flasher_ui_state_t *st, int pct)
{
    if (!st) return;
    st->progress = flasher_clamp_progress(pct);
}

void flasher_ui_set_status(flasher_ui_state_t *st, flasher_ui_status_t status)
{
    if (!st) return;
    st->status = status;
}

void flasher_ui_build_marquee(char *buf, size_t buf_size,
                              uint32_t elapsed_ms, bool is_bridge)
{
    if (!buf || buf_size == 0) return;
    const char *base = is_bridge ? "BRIDGE" : "FLASHING";
    int dots = (int)((elapsed_ms % 1200) / 300);
    switch (dots) {
    case 0:
        snprintf(buf, buf_size, "%s", base);
        break;
    case 1:
        snprintf(buf, buf_size, "%s.", base);
        break;
    case 2:
        snprintf(buf, buf_size, "%s..", base);
        break;
    case 3:
        snprintf(buf, buf_size, "%s...", base);
        break;
    default:
        snprintf(buf, buf_size, "%s", base);
        break;
    }
}

void flasher_ui_draw(const flasher_ui_state_t *st)
{
    if (!st) return;

    /* 计算进度条宽度 */
    int16_t bar_w = (int16_t)((HAL_SCREEN_WIDTH * st->progress) / 100);
    int16_t bar_y = 4;
    int16_t bar_h = HAL_SCREEN_HEIGHT - 8;

    /* 绘制左侧填充进度条 */
    if (bar_w > 0) {
        hal_draw_fill_rect(0, bar_y, bar_w, bar_h, COLOR_FG);
    }

    /* 如果进度未满，绘制右侧空心矩形边框 */
    if (bar_w < HAL_SCREEN_WIDTH) {
        hal_draw_rect(bar_w, bar_y, (int16_t)(HAL_SCREEN_WIDTH - bar_w), bar_h, COLOR_FG);
    }

    /* 确定显示文字 */
    char text[32];
    if (st->status == FLASHER_UI_BRIDGE || st->status == FLASHER_UI_FLASHING) {
        uint32_t elapsed_ms = hal_get_ticks() - st->start_ms;
        bool is_bridge = (st->status == FLASHER_UI_BRIDGE);
        flasher_ui_build_marquee(text, sizeof(text), elapsed_ms, is_bridge);
    } else if (st->status == FLASHER_UI_SUCCESS) {
        strncpy(text, "SUCCESS!", sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    } else {
        strncpy(text, "FAILED", sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }

    /* 计算文字居中位置 */
    int16_t tw = hal_get_string_width(text);
    int16_t fh = hal_get_font_height();
    int16_t tx = (HAL_SCREEN_WIDTH - tw) / 2;
    int16_t ty = (HAL_SCREEN_HEIGHT - fh) / 2;

    /* 反色逻辑：文字中心点是否在进度条内部 */
    int16_t text_center = tx + tw / 2;
    uint16_t color = (text_center < bar_w) ? COLOR_BG : flasher_ui_text_color(st->status);

    hal_draw_string(tx, ty, text, color);
}

#ifdef __cplusplus
}
#endif
