#include "oscilloscope_ui.h"
#include "oscilloscope_engine.h"

#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include "hal/hal_screen.h"

#include <stdio.h>

/* Colors (RGB565) */
#define SCOPE_COL_WAVE      0x07FF  /* cyan */
#define SCOPE_COL_WAVE_DIM  0x0144  /* dim cyan trail */
#define SCOPE_COL_GRID      0x18E3  /* dark blue-grey */
#define SCOPE_COL_GRID_HI   0x39E7  /* brighter grid */
#define SCOPE_COL_TRIGGER   0xF800  /* red trigger line */
#define SCOPE_COL_TEXT      COLOR_FG
#define SCOPE_COL_TEXT_HI   0xFFE0  /* yellow highlight */

/* Dynamic layout, computed each frame from actual font height */
static struct {
    int16_t header_h;
    int16_t wave_y;
    int16_t wave_h;
    int16_t footer_y;
    int16_t footer_h;
} s_layout;

static void scope_compute_layout(void)
{
    int16_t fh = hal_get_font_height();
    if (fh < 8) {
        fh = 8;
    }

    s_layout.header_h = fh + 4;              /* 2px margin top + 2px bottom */
    s_layout.footer_h = fh * 2 + 6;          /* two lines + margins */
    s_layout.wave_y   = s_layout.header_h;
    s_layout.wave_h   = HAL_SCREEN_HEIGHT - s_layout.header_h - s_layout.footer_h;
    s_layout.footer_y = s_layout.wave_y + s_layout.wave_h;

    if (s_layout.wave_h < 24) {
        /* Fallback for very small screens: keep at least 24 px waveform */
        s_layout.wave_h = 24;
        s_layout.footer_y = HAL_SCREEN_HEIGHT - s_layout.footer_h;
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

static const char *scope_get_param_label(scope_param_t param)
{
    switch (param) {
    case PARAM_TIME_BASE:    return "Tbase";
    case PARAM_VOLT_RANGE:   return "Vdiv";
    case PARAM_COUPLING:     return "Cpl";
    case PARAM_TRIGGER_MODE: return "Trig";
    case PARAM_TRIGGER_LEVEL: return "Lvl";
    default:                 return "";
    }
}

static void scope_draw_header(const oscilloscope_view_state_t *state)
{
    char buf[32];
    const char *run_str = state->running ? "RUN" : "HOLD";
    uint16_t col = state->running ? COLOR_ACCENT : COLOR_RED;

    hal_draw_string(2, 2, run_str, col);

    /* Sample rate in compact form */
    uint32_t sr = state->sample_rate_hz;
    if (sr >= 1000) {
        snprintf(buf, sizeof(buf), "%lu.%lukHz",
                 (unsigned long)(sr / 1000),
                 (unsigned long)((sr % 1000) / 100));
    } else {
        snprintf(buf, sizeof(buf), "%luHz", (unsigned long)sr);
    }
    hal_draw_string(28, 2, buf, SCOPE_COL_TEXT);

    snprintf(buf, sizeof(buf), "%s", g_scope_coupling_labels[state->coupling_index]);
    int16_t w = hal_get_string_width(buf);
    hal_draw_string(HAL_SCREEN_WIDTH - w - 2, 2, buf, SCOPE_COL_TEXT);
}

static void scope_draw_footer(const oscilloscope_view_state_t *state)
{
    char buf[32];
    char val_buf[16];
    int16_t fh = hal_get_font_height();
    int16_t line1_y = s_layout.footer_y + 2;
    int16_t line2_y = line1_y + fh + 2;

    /* Line 1: current parameter name + value */
    const char *label = scope_get_param_label((scope_param_t)state->selected_param);
    const char *value = scope_get_param_value(state,
                                              (scope_param_t)state->selected_param,
                                              val_buf, sizeof(val_buf));
    snprintf(buf, sizeof(buf), "%s:%s", label, value);

    if (state->editing) {
        int16_t w = hal_get_string_width(buf);
        hal_draw_fill_rect(1, line1_y - 1, w + 4, fh + 2, SCOPE_COL_WAVE);
        hal_draw_string(3, line1_y, buf, COLOR_BG);
    } else {
        hal_draw_string(2, line1_y, buf,
                        state->selected_param == PARAM_TIME_BASE ?
                        SCOPE_COL_TEXT_HI : SCOPE_COL_WAVE);
    }

    /* Line 2: trigger mode on the right */
    snprintf(buf, sizeof(buf), "T:%s",
             g_scope_trigger_mode_labels[state->trigger_mode_index]);
    int16_t tm_w = hal_get_string_width(buf);
    hal_draw_string(HAL_SCREEN_WIDTH - tm_w - 2, line2_y, buf, SCOPE_COL_TEXT);

    /* Line 2: measurements on the left */
    snprintf(buf, sizeof(buf), "Vpp:%d F:%lu",
             state->vpp_raw, (unsigned long)state->freq_hz);
    hal_draw_string(2, line2_y, buf, SCOPE_COL_TEXT);
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
