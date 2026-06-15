#include "oscilloscope_ui.h"
#include "oscilloscope_engine.h"

#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include "hal/hal_screen.h"

#include <stdio.h>

/* Landscape layout */
#define SCOPE_HEADER_H  12
#define SCOPE_WAVE_Y    12
#define SCOPE_WAVE_H    50
#define SCOPE_FOOTER_Y  64
#define SCOPE_FOOTER_H  16
#define SCOPE_PARAM_WIDTH 31

/* Colors (RGB565) */
#define SCOPE_COL_WAVE      0x07FF  /* cyan */
#define SCOPE_COL_WAVE_DIM  0x0144  /* dim cyan trail */
#define SCOPE_COL_GRID      0x18E3  /* dark blue-grey */
#define SCOPE_COL_GRID_HI   0x39E7  /* brighter grid */
#define SCOPE_COL_TRIGGER   0xF800  /* red trigger line */
#define SCOPE_COL_TEXT      COLOR_FG
#define SCOPE_COL_TEXT_HI   0xFFE0  /* yellow highlight */

static int16_t scope_map_y(uint16_t raw, uint16_t full_scale, int16_t wave_h)
{
    if (raw >= full_scale) {
        raw = full_scale - 1;
    }
    int16_t py = (int16_t)(((uint32_t)raw * (uint32_t)wave_h) / full_scale);
    return (SCOPE_WAVE_Y + wave_h - 1) - py;
}

static void scope_draw_grid(void)
{
    uint32_t t = hal_get_ticks() % 2000;
    uint16_t breath = (t < 1000) ? (uint16_t)t : (uint16_t)(2000 - t);
    uint16_t grid_col = (breath > 500) ? SCOPE_COL_GRID_HI : SCOPE_COL_GRID;

    for (int16_t yy = SCOPE_WAVE_Y + 10; yy < SCOPE_WAVE_Y + SCOPE_WAVE_H; yy += 10) {
        for (int16_t xx = 0; xx < HAL_SCREEN_WIDTH; xx += 4) {
            hal_draw_pixel(xx, yy, grid_col);
        }
    }
    for (int16_t xx = 16; xx < HAL_SCREEN_WIDTH; xx += 16) {
        for (int16_t yy = SCOPE_WAVE_Y; yy < SCOPE_WAVE_Y + SCOPE_WAVE_H; yy += 4) {
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

    hal_set_clip_rect(0, SCOPE_WAVE_Y, HAL_SCREEN_WIDTH, SCOPE_WAVE_H);

    int16_t prev_x = 0;
    int16_t prev_y = scope_map_y(state->samples[start], full_scale, SCOPE_WAVE_H);

    for (int16_t px = 1; px < HAL_SCREEN_WIDTH; px++) {
        uint32_t idx = start + (uint32_t)px * spp;
        if (idx >= state->sample_count) {
            break;
        }
        uint16_t raw = state->samples[idx];
        int16_t cur_y = scope_map_y(raw, full_scale, SCOPE_WAVE_H);
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
    int16_t ty = scope_map_y((uint16_t)state->trigger_level, full_scale, SCOPE_WAVE_H);

    for (int16_t xx = 0; xx < HAL_SCREEN_WIDTH; xx += 4) {
        hal_draw_pixel(xx, ty, SCOPE_COL_TRIGGER);
    }
}

static void scope_draw_header(const oscilloscope_view_state_t *state)
{
    char buf[32];
    const char *run_str = state->running ? "RUN" : "HOLD";
    uint16_t col = state->running ? COLOR_ACCENT : COLOR_RED;

    hal_draw_string(2, 2, run_str, col);

    snprintf(buf, sizeof(buf), "%luHz", (unsigned long)state->sample_rate_hz);
    hal_draw_string(28, 2, buf, SCOPE_COL_TEXT);

    snprintf(buf, sizeof(buf), "T:%s", g_scope_trigger_mode_labels[state->trigger_mode_index]);
    hal_draw_string(68, 2, buf, SCOPE_COL_TEXT);

    snprintf(buf, sizeof(buf), "%s", g_scope_coupling_labels[state->coupling_index]);
    int16_t w = hal_get_string_width(buf);
    hal_draw_string(HAL_SCREEN_WIDTH - w - 2, 2, buf, SCOPE_COL_TEXT);
}

static void scope_draw_footer(const oscilloscope_view_state_t *state)
{
    char buf[32];
    int16_t y = SCOPE_FOOTER_Y + 2;

    for (int i = 0; i < PARAM_COUNT; i++) {
        bool selected = (i == state->selected_param);
        bool editing = selected && state->editing;
        uint16_t col = selected ? (editing ? SCOPE_COL_TEXT_HI : SCOPE_COL_WAVE) : SCOPE_COL_GRID_HI;
        const char *val = "";

        switch (i) {
        case PARAM_TIME_BASE:
            val = g_scope_time_bases[state->time_base_index].label;
            break;
        case PARAM_VOLT_RANGE:
            val = g_scope_volt_ranges[state->volt_range_index].label;
            break;
        case PARAM_COUPLING:
            val = g_scope_coupling_labels[state->coupling_index];
            break;
        case PARAM_TRIGGER_MODE:
            val = g_scope_trigger_mode_labels[state->trigger_mode_index];
            break;
        case PARAM_TRIGGER_LEVEL:
            snprintf(buf, sizeof(buf), "%d", state->trigger_level);
            val = buf;
            break;
        }

        int16_t px = 2 + i * SCOPE_PARAM_WIDTH;
        if (px + SCOPE_PARAM_WIDTH - 1 > HAL_SCREEN_WIDTH) {
            break;
        }

        if (editing) {
            hal_draw_fill_rect(px, y - 1, SCOPE_PARAM_WIDTH - 1, 10, SCOPE_COL_WAVE);
            hal_draw_string(px + 1, y, val, COLOR_BG);
        } else {
            hal_draw_string(px + 1, y, val, col);
        }
    }

    snprintf(buf, sizeof(buf), "Vpp:%d F:%lu",
             state->vpp_raw, (unsigned long)state->freq_hz);
    int16_t mw = hal_get_string_width(buf);
    hal_draw_string(HAL_SCREEN_WIDTH - mw - 2, y + 8, buf, SCOPE_COL_TEXT);
}

void oscilloscope_ui_draw(const oscilloscope_view_state_t *state)
{
    scope_draw_grid();
    scope_draw_wave(state);
    scope_draw_trigger_line(state);
    scope_draw_header(state);
    scope_draw_footer(state);
}
