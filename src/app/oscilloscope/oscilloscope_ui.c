#include "oscilloscope_ui.h"
#include "oscilloscope_engine.h"

#include "hal/hal_display.h"
#include "hal/hal_layout.h"
#include "hal/hal_system.h"
#include "hal/hal_screen.h"

#include <stdio.h>
#include <string.h>

/* Colors (RGB565) */
#define SCOPE_COL_WAVE      0x07FF  /* cyan */
#define SCOPE_COL_WAVE_DIM  0x0144  /* dim cyan trail */
#define SCOPE_COL_GRID      0x18E3  /* dark blue-grey */
#define SCOPE_COL_GRID_HI   0x39E7  /* brighter grid */
#define SCOPE_COL_TRIGGER   0xF800  /* red trigger line */
#define SCOPE_COL_TEXT      COLOR_FG
#define SCOPE_COL_TEXT_HI   0xFFE0  /* yellow highlight */

/* 参考 taskmgr_ui.c / sm_ui.c：使用标准行高 */
static struct {
    int16_t header_h;
    int16_t wave_y;
    int16_t wave_h;
    int16_t footer_y;
    int16_t footer_h;
} s_layout;

static void scope_compute_layout(void)
{
    s_layout.header_h = HAL_ROW_H();
    s_layout.footer_h = HAL_ROW_H();
    s_layout.wave_y   = HAL_HEADER_BOTTOM();
    s_layout.wave_h   = (int16_t)(HAL_SCREEN_HEIGHT - HAL_ROW_H() * 2);
    s_layout.footer_y = HAL_FOOTER_TOP();

    if (s_layout.wave_h < 24) {
        s_layout.wave_h = 24;
    }
}

static int16_t scope_map_y(uint16_t raw, uint16_t full_scale)
{
    if (raw >= full_scale) {
        raw = full_scale - 1;
    }
    int16_t py = (int16_t)(((uint32_t)raw * (uint32_t)s_layout.wave_h) / full_scale);
    return (s_layout.wave_y + s_layout.wave_h - 1) - py;
}

static void scope_draw_grid(void)
{
    uint32_t t = hal_get_ticks() % 2000;
    uint16_t breath = (t < 1000) ? (uint16_t)t : (uint16_t)(2000 - t);
    uint16_t grid_col = (breath > 500) ? SCOPE_COL_GRID_HI : SCOPE_COL_GRID;

    int16_t grid_step_y = s_layout.wave_h / 4;
    if (grid_step_y < 8) {
        grid_step_y = 8;
    }
    int16_t grid_step_x = HAL_SCREEN_WIDTH / 5;
    if (grid_step_x < 16) {
        grid_step_x = 16;
    }

    for (int16_t yy = s_layout.wave_y + grid_step_y;
         yy < s_layout.wave_y + s_layout.wave_h;
         yy += grid_step_y) {
        for (int16_t xx = 0; xx < HAL_SCREEN_WIDTH; xx += 4) {
            hal_draw_pixel(xx, yy, grid_col);
        }
    }
    for (int16_t xx = grid_step_x; xx < HAL_SCREEN_WIDTH; xx += grid_step_x) {
        for (int16_t yy = s_layout.wave_y;
             yy < s_layout.wave_y + s_layout.wave_h;
             yy += 4) {
            hal_draw_pixel(xx, yy, grid_col);
        }
    }
}

static void scope_draw_wave(const oscilloscope_view_state_t *state)
{
    if (state->samples == NULL || state->sample_count == 0) {
        return;
    }

    uint16_t full_scale = g_scope_volt_ranges[state->volt_range_index].full_scale;
    uint8_t spp = g_scope_time_bases[state->time_base_index].samples_per_pixel;
    uint16_t start = 0;
    if (state->trigger_index != 0xFFFF && state->trigger_index < state->sample_count) {
        start = state->trigger_index;
    }

    hal_set_clip_rect(0, s_layout.wave_y, HAL_SCREEN_WIDTH, s_layout.wave_h);

    int16_t prev_x = 0;
    int16_t prev_y = scope_map_y(state->samples[start], full_scale);

    for (int16_t px = 1; px < HAL_SCREEN_WIDTH; px++) {
        uint32_t idx = start + (uint32_t)px * spp;
        if (idx >= state->sample_count) {
            break;
        }
        uint16_t raw = state->samples[idx];
        int16_t cur_y = scope_map_y(raw, full_scale);
        int16_t cur_x = px;

        hal_draw_line(prev_x, prev_y, cur_x, cur_y, SCOPE_COL_WAVE_DIM);
        hal_draw_pixel(cur_x, cur_y, SCOPE_COL_WAVE);

        prev_x = cur_x;
        prev_y = cur_y;
    }

    hal_clear_clip_rect();
}

static void scope_draw_trigger_line(const oscilloscope_view_state_t *state)
{
    uint16_t full_scale = g_scope_volt_ranges[state->volt_range_index].full_scale;
    int16_t ty = scope_map_y((uint16_t)state->trigger_level, full_scale);

    for (int16_t xx = 0; xx < HAL_SCREEN_WIDTH; xx += 4) {
        hal_draw_pixel(xx, ty, SCOPE_COL_TRIGGER);
    }
}

static const char *scope_get_param_value(const oscilloscope_view_state_t *state,
                                         scope_param_t param, char *buf, size_t buf_size)
{
    switch (param) {
    case PARAM_TIME_BASE:
        return g_scope_time_bases[state->time_base_index].label;
    case PARAM_VOLT_RANGE:
        return g_scope_volt_ranges[state->volt_range_index].label;
    case PARAM_COUPLING:
        return g_scope_coupling_labels[state->coupling_index];
    case PARAM_TRIGGER_MODE:
        return g_scope_trigger_mode_labels[state->trigger_mode_index];
    case PARAM_TRIGGER_LEVEL:
        snprintf(buf, buf_size, "%d", state->trigger_level);
        return buf;
    default:
        return "";
    }
}

/* 参考 sm_ui.c：居中绘制字符串，自动计算间距 */
static void scope_draw_header(const oscilloscope_view_state_t *state)
{
    char buf[32];
    int16_t fh = hal_get_font_height();
    int16_t ty = HAL_TEXT_BASELINE(HAL_HEADER_TOP()) - 2;

    const char *run_str = state->running ? "RUN" : "HOLD";
    uint16_t run_col = state->running ? COLOR_ACCENT : COLOR_RED;

    /* 右侧：耦合方式 */
    snprintf(buf, sizeof(buf), "%s", g_scope_coupling_labels[state->coupling_index]);
    int16_t right_w = hal_get_string_width(buf);
    int16_t right_x = HAL_SCREEN_WIDTH - right_w - HAL_MARGIN_MD;

    /* 中间：采样率 */
    uint32_t sr = state->sample_rate_hz;
    if (sr >= 1000) {
        snprintf(buf, sizeof(buf), "%lu.%lukHz",
                 (unsigned long)(sr / 1000),
                 (unsigned long)((sr % 1000) / 100));
    } else {
        snprintf(buf, sizeof(buf), "%luHz", (unsigned long)sr);
    }
    int16_t mid_w = hal_get_string_width(buf);

    int16_t total_w = hal_get_string_width(run_str) + mid_w + right_w;
    int16_t spacing = (HAL_SCREEN_WIDTH - HAL_MARGIN_MD * 2 - total_w) / 2;
    if (spacing < HAL_MARGIN_SM) {
        spacing = HAL_MARGIN_SM;
    }

    int16_t run_x = HAL_MARGIN_MD;
    int16_t mid_x = run_x + hal_get_string_width(run_str) + spacing;
    if (mid_x + mid_w > right_x - HAL_MARGIN_SM) {
        /* 中间太宽，居中绘制并裁剪 */
        mid_x = HAL_CENTER_X(mid_w);
    }

    hal_draw_string(run_x, ty, run_str, run_col);
    hal_draw_string(mid_x, ty, buf, SCOPE_COL_TEXT);
    hal_draw_string(right_x, ty, g_scope_coupling_labels[state->coupling_index], SCOPE_COL_TEXT);

    /* 分隔线 */
    hal_draw_line(0, HAL_HEADER_BOTTOM(), HAL_SCREEN_WIDTH, HAL_HEADER_BOTTOM(), COLOR_FG);
}

static void scope_draw_footer(const oscilloscope_view_state_t *state)
{
    char buf[32];
    char val_buf[16];
    int16_t fh = hal_get_font_height();
    int16_t ty = HAL_TEXT_BASELINE(HAL_FOOTER_TOP()) - 2;

    /* 左侧：当前选中参数（编辑时反色） */
    const char *label = NULL;
    switch (state->selected_param) {
    case PARAM_TIME_BASE:     label = "Tbase"; break;
    case PARAM_VOLT_RANGE:    label = "Vdiv";  break;
    case PARAM_COUPLING:      label = "Cpl";   break;
    case PARAM_TRIGGER_MODE:  label = "Trig";  break;
    case PARAM_TRIGGER_LEVEL: label = "Lvl";   break;
    default:                  label = "";      break;
    }
    const char *value = scope_get_param_value(state,
                                              (scope_param_t)state->selected_param,
                                              val_buf, sizeof(val_buf));
    snprintf(buf, sizeof(buf), "%s:%s", label, value);

    /* 右侧：触发模式 + 测量值 */
    char right_buf[32];
    snprintf(right_buf, sizeof(right_buf), "T:%s V:%d F:%lu",
             g_scope_trigger_mode_labels[state->trigger_mode_index],
             state->vpp_raw,
             (unsigned long)state->freq_hz);
    int16_t right_w = hal_get_string_width(right_buf);
    int16_t right_x = HAL_SCREEN_WIDTH - right_w - HAL_MARGIN_MD;

    int16_t param_w = hal_get_string_width(buf);
    if (state->editing) {
        int16_t bg_x = HAL_MARGIN_MD - 1;
        int16_t bg_w = param_w + HAL_MARGIN_MD;
        if (bg_w > right_x - HAL_MARGIN_MD) {
            bg_w = right_x - HAL_MARGIN_MD;
        }
        hal_draw_fill_rect(bg_x, HAL_FOOTER_TOP() + 1, bg_w, s_layout.footer_h - 2, SCOPE_COL_WAVE);
        hal_draw_string(HAL_MARGIN_MD, ty, buf, COLOR_BG);
    } else {
        hal_draw_string(HAL_MARGIN_MD, ty, buf, SCOPE_COL_TEXT);
    }

    hal_draw_string(right_x, ty, right_buf, SCOPE_COL_TEXT);

    /* 分隔线 */
    hal_draw_line(0, HAL_FOOTER_TOP(), HAL_SCREEN_WIDTH, HAL_FOOTER_TOP(), COLOR_FG);
}

void oscilloscope_ui_draw(const oscilloscope_view_state_t *state)
{
    scope_compute_layout();
    scope_draw_grid();
    scope_draw_wave(state);
    scope_draw_trigger_line(state);
    scope_draw_header(state);
    scope_draw_footer(state);
}
