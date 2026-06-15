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
#define SCOPE_COL_WAVE_DIM  0x03A6  /* brighter cyan trail */
#define SCOPE_COL_WAVE_PEAK 0xFFFF  /* white peak dot */
#define SCOPE_COL_GRID      0x39E7  /* brighter blue-grey grid */
#define SCOPE_COL_TRIGGER   0xF800  /* red trigger line */
#define SCOPE_COL_TEXT      COLOR_FG
#define SCOPE_COL_TEXT_HI   0xFFE0  /* yellow highlight */

static struct {
    int16_t header_h;
    int16_t wave_y;
    int16_t wave_h;
    int16_t footer_y;
    int16_t footer_h;
} s_layout;

static void scope_compute_layout(void)
{
    static bool s_layout_valid = false;

    if (s_layout_valid) {
        return;
    }

    s_layout.header_h = HAL_ROW_H();
    s_layout.footer_h = HAL_ROW_H();
    s_layout.wave_y   = HAL_HEADER_BOTTOM();
    s_layout.wave_h   = (int16_t)(HAL_SCREEN_HEIGHT - HAL_ROW_H() * 2);
    s_layout.footer_y = HAL_FOOTER_TOP();

    if (s_layout.wave_h < 24) {
        s_layout.wave_h = 24;
    }

    s_layout_valid = true;
}

static int16_t scope_map_y(uint16_t raw, uint16_t full_scale)
{
    if (raw > full_scale) {
        raw = full_scale;
    }
    /* wave_h-1 确保 raw=full_scale 时映射到 wave 区域顶部，而不会溢出 */
    int16_t py = (int16_t)(((uint32_t)raw * (uint32_t)(s_layout.wave_h - 1)) / full_scale);
    return (s_layout.wave_y + s_layout.wave_h - 1) - py;
}

static void scope_draw_grid(void)
{
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
            hal_draw_pixel(xx, yy, SCOPE_COL_GRID);
        }
    }
    for (int16_t xx = grid_step_x; xx < HAL_SCREEN_WIDTH; xx += grid_step_x) {
        for (int16_t yy = s_layout.wave_y;
             yy < s_layout.wave_y + s_layout.wave_h;
             yy += 4) {
            hal_draw_pixel(xx, yy, SCOPE_COL_GRID);
        }
    }
}

static void scope_draw_wave(const oscilloscope_view_state_t *state)
{
    if (state->samples == NULL || state->sample_count == 0) {
        return;
    }

    uint16_t full_scale = g_scope_volt_ranges[state->volt_range_index].full_scale;
    uint16_t start = 0;
    if (state->trigger_index != 0xFFFF && state->trigger_index < state->sample_count) {
        start = state->trigger_index;
    }

    uint16_t remaining = state->sample_count - start;
    if (remaining < 2) {
        return;
    }

    hal_set_clip_rect(0, s_layout.wave_y, HAL_SCREEN_WIDTH, s_layout.wave_h);

    int16_t prev_x = 0;
    int16_t prev_y = scope_map_y(state->samples[start], full_scale);

    for (int16_t px = 1; px < HAL_SCREEN_WIDTH; px++) {
        uint32_t idx = start + ((uint32_t)px * (uint32_t)remaining) / (uint32_t)HAL_SCREEN_WIDTH;
        if (idx >= state->sample_count) {
            idx = state->sample_count - 1U;
        }
        uint16_t raw = state->samples[idx];
        int16_t cur_y = scope_map_y(raw, full_scale);
        int16_t cur_x = px;

        /* 主线更亮，轮廓辅助线更粗，增强小信号可视度 */
        hal_draw_line(prev_x, prev_y - 1, cur_x, cur_y - 1, SCOPE_COL_WAVE_DIM);
        hal_draw_line(prev_x, prev_y + 1, cur_x, cur_y + 1, SCOPE_COL_WAVE_DIM);
        hal_draw_line(prev_x, prev_y, cur_x, cur_y, SCOPE_COL_WAVE);
        hal_draw_pixel(cur_x, cur_y, SCOPE_COL_WAVE_PEAK);

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
    case PARAM_SAMPLE_RATE:
        return g_scope_sample_rates[state->sample_rate_index].label;
    case PARAM_FILTER:
        return g_scope_filters[state->filter_index].label;
    default:
        return "";
    }
}

static const char *scope_get_param_label(scope_param_t param)
{
    switch (param) {
    case PARAM_TIME_BASE:     return "T";
    case PARAM_VOLT_RANGE:    return "V";
    case PARAM_COUPLING:      return "C";
    case PARAM_TRIGGER_MODE:  return "Tr";
    case PARAM_TRIGGER_LEVEL: return "Lv";
    case PARAM_SAMPLE_RATE:   return "SR";
    case PARAM_FILTER:        return "Flt";
    default:                  return "";
    }
}

static void scope_format_rate(char *buf, size_t size, uint32_t sr)
{
    if (sr >= 1000000) {
        snprintf(buf, size, "%luMHz", (unsigned long)(sr / 1000000));
    } else if (sr >= 1000) {
        if (sr % 1000 == 0) {
            snprintf(buf, size, "%lukHz", (unsigned long)(sr / 1000));
        } else {
            snprintf(buf, size, "%lu.%lukHz",
                     (unsigned long)(sr / 1000),
                     (unsigned long)((sr % 1000) / 100));
        }
    } else {
        snprintf(buf, size, "%luHz", (unsigned long)sr);
    }
}

static void scope_draw_header(const oscilloscope_view_state_t *state)
{
    char buf[32];
    int16_t ty = HAL_TEXT_BASELINE(HAL_HEADER_TOP());

    const char *run_str = state->running ? "RUN" : "HOLD";
    uint16_t run_col = state->running ? COLOR_ACCENT : COLOR_RED;

    /* 四个字段，从左到右：RUN | 采样率 | 触发模式 | 耦合 */
    scope_format_rate(buf, sizeof(buf), state->sample_rate_hz);
    int16_t w_rate = hal_get_string_width(buf);

    int16_t w_run = hal_get_string_width(run_str);
    int16_t w_trig = hal_get_string_width(g_scope_trigger_mode_labels[state->trigger_mode_index]);
    int16_t w_cpl = hal_get_string_width(g_scope_coupling_labels[state->coupling_index]);

    int16_t total_w = w_run + w_rate + w_trig + w_cpl;
    int16_t avail = HAL_SCREEN_WIDTH - HAL_MARGIN_MD * 2;
    int16_t spacing = (avail - total_w) / 3;
    if (spacing < HAL_MARGIN_SM) {
        spacing = HAL_MARGIN_SM;
    }

    int16_t x = HAL_MARGIN_MD;
    hal_draw_string(x, ty, run_str, run_col);
    x += w_run + spacing;

    hal_draw_string(x, ty, buf, SCOPE_COL_TEXT);
    x += w_rate + spacing;

    hal_draw_string(x, ty, g_scope_trigger_mode_labels[state->trigger_mode_index], SCOPE_COL_TEXT);
    x += w_trig + spacing;

    /* 右侧对齐耦合，避免超出 */
    int16_t cpl_x = HAL_SCREEN_WIDTH - w_cpl - HAL_MARGIN_MD;
    if (cpl_x > x) {
        hal_draw_string(cpl_x, ty, g_scope_coupling_labels[state->coupling_index], SCOPE_COL_TEXT);
    }

    /* 分隔线 */
    hal_draw_h_line(0, HAL_HEADER_BOTTOM(), HAL_SCREEN_WIDTH, COLOR_FG);
}

static void scope_draw_footer(const oscilloscope_view_state_t *state)
{
    char buf[32];
    char val_buf[16];
    int16_t ty = HAL_TEXT_BASELINE(HAL_FOOTER_TOP());

    /* 左侧：当前选中参数（编辑时反色） */
    const char *label = scope_get_param_label((scope_param_t)state->selected_param);
    const char *value = scope_get_param_value(state,
                                              (scope_param_t)state->selected_param,
                                              val_buf, sizeof(val_buf));
    snprintf(buf, sizeof(buf), "%s:%s", label, value);
    int16_t param_w = hal_get_string_width(buf);

    /* 右侧：测量值，固定宽度并右对齐，避免数字位数变化时抖动
     * Vpp / Vavg 显示为 mV，范围 0~3300 mV */
    char right_buf[32];
    const char *v_label = (state->selected_param == PARAM_VOLT_RANGE) ? "Vavg" : "Vpp";
    uint16_t v_value = (state->selected_param == PARAM_VOLT_RANGE)
                           ? state->vavg_mv
                           : state->vpp_mv;
    snprintf(right_buf, sizeof(right_buf), "%s:%dmV F:%lu",
             v_label, v_value, (unsigned long)state->freq_hz);
    int16_t right_w = hal_get_string_width(right_buf);

    /* 按最坏情况预留宽度：Vpp:3300mV F:99999 */
    char right_max[32];
    snprintf(right_max, sizeof(right_max), "Vpp:3300mV F:%lu", 99999UL);
    int16_t right_w_max = hal_get_string_width(right_max);

    int16_t right_x = HAL_SCREEN_WIDTH - right_w_max - HAL_MARGIN_MD;
    int16_t draw_x = right_x + (right_w_max - right_w);

    if (state->editing) {
        int16_t bg_w = param_w + HAL_MARGIN_MD;
        if (bg_w > right_x - HAL_MARGIN_MD) {
            bg_w = right_x - HAL_MARGIN_MD;
        }
        hal_draw_fill_rect(HAL_MARGIN_MD - 1, HAL_FOOTER_TOP() + 1,
                           bg_w, s_layout.footer_h - 2, COLOR_FG);
        hal_draw_string(HAL_MARGIN_MD, ty, buf, COLOR_BG);
    } else {
        hal_draw_string(HAL_MARGIN_MD, ty, buf, SCOPE_COL_TEXT);
    }

    hal_draw_string(draw_x, ty, right_buf, SCOPE_COL_TEXT);

    /* 分隔线 */
    hal_draw_h_line(0, HAL_FOOTER_TOP(), HAL_SCREEN_WIDTH, COLOR_FG);
}

static bool s_frame_dirty = true;

void oscilloscope_ui_mark_dirty(void)
{
    s_frame_dirty = true;
}

void oscilloscope_ui_draw(const oscilloscope_view_state_t *state)
{
    if (!s_frame_dirty) {
        return;
    }

    scope_compute_layout();
    scope_draw_grid();
    scope_draw_wave(state);
    scope_draw_trigger_line(state);
    scope_draw_header(state);
    scope_draw_footer(state);

    if (!state->running && !state->editing) {
        s_frame_dirty = false;
    }
}
