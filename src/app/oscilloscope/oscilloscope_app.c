/**
 * @file   oscilloscope_app.c
 * @brief  示波器 App ADC 采样、触发与测量引擎
 * @details 实现 ADC 采样环形缓冲区、上升沿触发、AC/GND 耦合输出以及
 *          Vpp / 平均值 / 频率测量。本文件不包含生命周期与输入处理。
 *
 * @copyright Copyright (c) 2026
 */

#include "oscilloscope.h"
#include "oscilloscope_engine.h"
#include "oscilloscope_ui.h"
#include "oscilloscope_internal.h"
#include "oscilloscope_input.h"

#include "app/ui_service.h"
#include "hal/hal_input.h"
#include "hal/hal_screen.h"
#include "ui/ui_item.h"

#include <string.h>

#ifndef NATIVE_TEST
#include <Arduino.h>
#endif

#define SCOPE_PIN 36

/* ═══ 参数表（供 UI 层与测试使用）═══ */

const scope_time_base_t g_scope_time_bases[SCOPE_TIME_BASE_COUNT] = {
    { "50us",  1,   20000 },
    { "100us", 2,   10000 },
    { "200us", 4,    5000 },
    { "500us", 10,   2000 },
    { "1ms",   20,   1000 },
    { "2ms",   40,    500 },
    { "5ms",   100,   200 },
};

const scope_volt_range_t g_scope_volt_ranges[SCOPE_VOLT_RANGE_COUNT] = {
    { "0.5V",  620 },
    { "1V",   1241 },
    { "2V",   2482 },
    { "3.3V", 4095 },
};

const char *g_scope_coupling_labels[SCOPE_COUPLING_COUNT] = { "DC", "AC", "GND" };

const char *g_scope_trigger_mode_labels[SCOPE_TRIGGER_MODE_COUNT] = { "Auto", "Norm", "Scan" };

/* ═══ 模块状态 ═══ */

scope_state_t g_scope = {
    .view = {
        .trigger_index    = 0xFFFF,
        .sample_rate_hz   = 20000,
        .trigger_level    = 2048,
        .time_base_index  = 0,
        .volt_range_index = 0,
        .coupling_index   = 0,
        .trigger_mode_index = 0,
        .running = true,
    }
};

static const uint16_t *scope_get_display_buffer(uint8_t coupling);
static void scope_update_measurements(const uint16_t *buf, uint16_t count);

/* ═══ Native 测试桩（仅测试环境）═══ */

#ifdef NATIVE_TEST
int __attribute__((weak)) analogRead(uint8_t pin)
{
    (void)pin;
    return 0;
}

void __attribute__((weak)) analogSetPinAttenuation(uint8_t pin, int atten)
{
    (void)pin;
    (void)atten;
}

void __attribute__((weak)) pinMode(uint8_t pin, uint8_t mode)
{
    (void)pin;
    (void)mode;
}
#endif /* NATIVE_TEST */

/* ═══ 采样 ═══ */

static void scope_sample_one(void)
{
    uint16_t pos = g_scope.sample_write_pos;

    g_scope.samples[pos] = (uint16_t)analogRead(SCOPE_PIN);
    g_scope.sample_write_pos = (pos + 1U) % SCOPE_SAMPLE_MAX;
}

/* ═══ 触发 ═══ */

uint16_t scope_find_trigger_rising(const uint16_t *buf, uint16_t count,
                                   uint16_t start, uint16_t level)
{
    if (buf == NULL || count == 0U || start >= count - 1U) {
        return 0xFFFF;
    }
    for (uint16_t i = start + 1U; i < count; i++) {
        if (buf[i - 1U] <= level && buf[i] > level) {
            return i;
        }
    }
    return 0xFFFF;
}

static uint16_t scope_clamp_trigger_level(int16_t level)
{
    if (level < 0) {
        return 0;
    }
    if (level > 4095) {
        return 4095;
    }
    return (uint16_t)level;
}

static void scope_update_trigger(void)
{
    scope_trigger_mode_t mode = (scope_trigger_mode_t)g_scope.view.trigger_mode_index;

    if (mode >= SCOPE_TRIGGER_MODE_COUNT || mode == SCOPE_TRIGGER_SCAN) {
        g_scope.view.trigger_index = 0;
    } else {
        uint16_t level = scope_clamp_trigger_level(g_scope.view.trigger_level);
        uint16_t idx   = scope_find_trigger_rising(g_scope.samples,
                                                   SCOPE_SAMPLE_MAX,
                                                   0, level);

        if (idx != 0xFFFF) {
            g_scope.view.trigger_index = idx;
        } else if (mode == SCOPE_TRIGGER_AUTO) {
            g_scope.view.trigger_index = 0;
        }
    }

    const uint16_t *buf = scope_get_display_buffer(g_scope.view.coupling_index);
    scope_update_measurements(buf, SCOPE_SAMPLE_MAX);
}

/* ═══ 耦合输出 ═══ */

static int16_t scope_compute_ac_offset(void)
{
    uint32_t sum = 0;
    uint16_t n   = SCOPE_AC_OFFSET_WINDOW;

    if (SCOPE_SAMPLE_MAX < n) {
        n = SCOPE_SAMPLE_MAX;
    }

    for (uint16_t i = 0; i < n; i++) {
        uint16_t idx = (g_scope.sample_write_pos - n + i + SCOPE_SAMPLE_MAX)
                       % SCOPE_SAMPLE_MAX;
        sum += g_scope.samples[idx];
    }

    return (int16_t)(sum / n);
}

static const uint16_t *scope_get_display_buffer(uint8_t coupling)
{
    if (coupling >= SCOPE_COUPLING_COUNT) {
        coupling = 0; /* default to DC */
    }

    if (coupling == 0U) {
        return g_scope.samples;
    }

    if (coupling == 2U) {
        memset(g_scope.ac_coupled, 0, sizeof(g_scope.ac_coupled));
        return g_scope.ac_coupled;
    }

    int16_t offset = scope_compute_ac_offset();
    g_scope.ac_offset = offset;

    for (uint16_t i = 0; i < SCOPE_SAMPLE_MAX; i++) {
        int32_t v = (int32_t)g_scope.samples[i] - offset;
        if (v < 0) {
            v = 0;
        } else if (v > 4095) {
            v = 4095;
        }
        g_scope.ac_coupled[i] = (uint16_t)v;
    }

    return g_scope.ac_coupled;
}

/* ═══ 测量 ═══ */

static void scope_update_measurements(const uint16_t *buf, uint16_t count)
{
    if (buf == NULL || count == 0U) {
        g_scope.view.vpp_raw = 0;
        g_scope.view.vavg_raw = 0;
        g_scope.view.freq_hz = 0;
        return;
    }

    uint32_t sum  = 0;
    uint16_t minv = 4095;
    uint16_t maxv = 0;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t v = buf[i];
        sum += v;
        if (v < minv) {
            minv = v;
        }
        if (v > maxv) {
            maxv = v;
        }
    }

    g_scope.view.vpp_raw = maxv - minv;
    g_scope.view.vavg_raw = (uint16_t)(sum / count);

    uint32_t sr = g_scope.view.sample_rate_hz;
    if (sr == 0) {
        g_scope.view.freq_hz = 0;
        return;
    }

    uint16_t avg = g_scope.view.vavg_raw;
    uint16_t crossings = 0;

    for (uint16_t i = 1; i < count; i++) {
        bool above0 = buf[i - 1U] >= avg;
        bool above1 = buf[i] >= avg;
        if (above0 != above1) {
            crossings++;
        }
    }

    g_scope.view.freq_hz = ((uint32_t)crossings * sr) / (2U * count);
}

/* ═══ 生命周期 ═══ */

void oscilloscope_init(void *user_data)
{
    (void)user_data;
    memset(&g_scope, 0, sizeof(g_scope));

    g_scope.view.samples = g_scope.samples;
    g_scope.view.sample_count = SCOPE_SAMPLE_MAX;
    g_scope.view.trigger_index = 0xFFFF;
    g_scope.view.sample_rate_hz = g_scope_time_bases[0].display_rate_hz;
    g_scope.view.running = true;
    g_scope.view.selected_param = PARAM_TIME_BASE;
    g_scope.view.trigger_level = 2048; /* mid-scale */

#ifndef NATIVE_TEST
    pinMode(SCOPE_PIN, INPUT);
    analogSetPinAttenuation(SCOPE_PIN, ADC_11db);
#endif

    ui_service_enter_landscape();
    ui_service_user_item_init();
}

void oscilloscope_exit(void *user_data)
{
    (void)user_data;
    ui_service_user_item_exit();
    ui_service_exit_landscape();
}

/* ═══ 主循环 ═══ */

void oscilloscope_loop(void *user_data)
{
    (void)user_data;

    if (g_scope.view.running) {
        uint8_t spp = g_scope_time_bases[g_scope.view.time_base_index].samples_per_pixel;
        uint16_t samples_to_take = (uint16_t)(HAL_SCREEN_WIDTH * spp);
        if (samples_to_take > SCOPE_SAMPLE_MAX) {
            samples_to_take = SCOPE_SAMPLE_MAX;
        }
        for (uint16_t i = 0; i < samples_to_take; i++) {
            scope_sample_one();
        }
        scope_update_trigger();
        scope_sync_time_base();
    }

    oscilloscope_ui_draw(&g_scope.view);

    hal_event_t ev_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t ev_b = hal_input_get_event(HAL_BTN_B);

    scope_handle_input(ev_a, ev_b);

    if (ui_user_item_try_exit(ev_b)) {
        return;
    }
}
