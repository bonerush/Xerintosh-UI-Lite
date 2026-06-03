/**
 * @file   tu_ui.cpp
 * @brief  Token Usage UI 渲染实现
 * @details 使用 hal_draw_string / hal_draw_h_line 在屏幕上绘制
 *          Deepseek 余额和 Kimi Token 用量信息。
 */

#include "tu_ui.h"

#include <stdio.h>

#include "hal/hal_display.h"
#include "hal/hal_layout.h"

/* ═══ 颜色常量 ═══ */

#define C_GRAY ((uint16_t)0x8410) /* RGB565 灰色，用于不可用/提示文字 */

/* ═══ 绘制 ═══ */

void tu_ui_draw(const tu_data_t *data, int selected) {
    (void)selected; /* 预留，暂未使用 */

    if (!data) {
        return;
    }

    const int16_t x = HAL_LEFT_X();
    const int16_t line_h = HAL_ROW_H();
    char buf[32];

    /* 起始 y = header 下方 */
    int16_t y = HAL_BODY_TOP() + HAL_MARGIN_LG;

    /* ── 标题 ── */
    hal_draw_string(x, HAL_TEXT_BASELINE(y), "Token Usage", COLOR_FG);
    y += line_h + HAL_MARGIN_SM;

    /* ── 分隔线 ── */
    hal_draw_h_line(x, y, SCREEN_WIDTH - 2 * x, COLOR_FG);
    y += HAL_MARGIN_LG;

    /* ── Deepseek 部分 ── */
    hal_draw_string(x, HAL_TEXT_BASELINE(y), "Deepseek:", COLOR_FG);
    y += line_h;

    snprintf(buf, sizeof(buf), "Balance: %.2f CNY", data->deepseek.total_balance);
    hal_draw_string(x + HAL_MARGIN_MD, HAL_TEXT_BASELINE(y),
                    buf, data->deepseek_ok ? COLOR_FG : C_GRAY);
    y += line_h;

    snprintf(buf, sizeof(buf), "Status: %s",
             data->deepseek.is_available ? "OK" : "Low");
    hal_draw_string(x + HAL_MARGIN_MD, HAL_TEXT_BASELINE(y),
                    buf, data->deepseek_ok ? COLOR_FG : C_GRAY);
    y += line_h + HAL_MARGIN_LG;

    /* ── Kimi 部分 ── */
    hal_draw_string(x, HAL_TEXT_BASELINE(y), "Kimi:", COLOR_FG);
    y += line_h;

    snprintf(buf, sizeof(buf), "Tokens: %.0f", data->kimi.daily_tokens);
    hal_draw_string(x + HAL_MARGIN_MD, HAL_TEXT_BASELINE(y),
                    buf, data->kimi_ok ? COLOR_FG : C_GRAY);
    y += line_h;

    snprintf(buf, sizeof(buf), "Limit: %.0f", data->kimi.rate_limit);
    hal_draw_string(x + HAL_MARGIN_MD, HAL_TEXT_BASELINE(y),
                    buf, data->kimi_ok ? COLOR_FG : C_GRAY);
    y += line_h + HAL_MARGIN_LG;

    /* ── 刷新提示 ── */
    hal_draw_string(x, HAL_TEXT_BASELINE(y), "BtnA: Refresh", C_GRAY);
}
