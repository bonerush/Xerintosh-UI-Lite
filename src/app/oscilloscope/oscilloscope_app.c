/**
 * @file   oscilloscope_app.c
 * @brief  Oscilloscope App ADC sampling, triggering, and measurement engine
 * @details Linearly collects sample_count samples per frame, searches for rising edge
 *          trigger within the acquisition window, AC/GND coupling output, and
 *          Vpp / average / frequency measurements.
 *          This file does not contain lifecycle or input handling.
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
#include "hal/hal_system.h"

#include <string.h>

#ifndef NATIVE_TEST
#include "driver/adc_oneshot.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#endif

#define SCOPE_PIN 36

/* ADC1 channel mapping for GPIO36 */
#define SCOPE_ADC_CHANNEL ADC_CHANNEL_0

/* ═══ Module State ═══ */

scope_state_t g_scope = {
    .view = {
        .trigger_index      = 0xFFFF,
        .trigger_level      = 2048,
        .time_base_index    = 0,
        .volt_range_index   = 0,
        .coupling_index     = 0,
        .trigger_mode_index = 0,
        .sample_rate_index  = 3,   /* 20kHz */
        .filter_index       = 1,   /* Low EMA */
        .running = true,
    },
    .last_filtered = 2048,
};

static const uint16_t *scope_get_display_buffer(uint8_t coupling);
static void scope_update_measurements(const uint16_t *buf, uint16_t count);

/* ═══ Native test stubs (test environment only) ═══ */

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

void __attribute__((weak)) delayMicroseconds(uint32_t us)
{
    (void)us;
}
#endif /* NATIVE_TEST */

#ifndef NATIVE_TEST
/* ═══ ESP-IDF ADC handle (module scope) ═══ */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
#endif

/* ═══ Sampling ═══ */

static void scope_sample_one(uint16_t pos)
{
#ifndef NATIVE_TEST
    int raw_adc = 0;
    if (s_adc_handle != NULL) {
        adc_oneshot_read(s_adc_handle, SCOPE_ADC_CHANNEL, &raw_adc);
    }
    uint16_t raw = (uint16_t)raw_adc;
#else
    uint16_t raw = (uint16_t)analogRead(SCOPE_PIN);
#endif

    uint8_t fidx = g_scope.view.filter_index;
    if (fidx >= SCOPE_FILTER_COUNT) {
        fidx = 0;
    }

    uint8_t prev_w = g_scope_filters[fidx].prev_weight;
    uint16_t filtered;
    if (prev_w == 0U) {
        filtered = raw;
    } else {
        uint8_t cur_w = 8U - prev_w;
        filtered = (uint16_t)(((uint32_t)g_scope.last_filtered * prev_w +
                               (uint32_t)raw * cur_w) / 8U);
    }

    g_scope.last_filtered = filtered;
    g_scope.samples[pos] = filtered;
}

/* ═══ Trigger ═══ */

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
    uint16_t count = g_scope.view.sample_count;

    if (count == 0U) {
        g_scope.view.trigger_index = 0xFFFF;
    } else if (mode >= SCOPE_TRIGGER_MODE_COUNT || mode == SCOPE_TRIGGER_SCAN) {
        g_scope.view.trigger_index = 0;
    } else {
        uint16_t level = scope_clamp_trigger_level(g_scope.view.trigger_level);
        uint16_t idx   = scope_find_trigger_rising(g_scope.samples,
                                                   count, 0, level);

        if (idx != 0xFFFF) {
            g_scope.view.trigger_index = idx;
        } else if (mode == SCOPE_TRIGGER_AUTO) {
            g_scope.view.trigger_index = 0;
        }
    }

    const uint16_t *buf = scope_get_display_buffer(g_scope.view.coupling_index);
    scope_update_measurements(buf, count);
}

/* ═══ Coupling Output ═══ */

static int16_t scope_compute_ac_offset(void)
{
    uint32_t sum = 0;
    uint16_t count = g_scope.view.sample_count;
    uint16_t n;
    uint16_t i;

    if (count == 0U) {
        return 2048;
    }

    /* AC offset window takes 1/10 frame length or minimum of 32, covering ~1 signal period */
    n = count / 10U;
    if (n < 8U) {
        n = 8U;
    }
    if (n > count) {
        n = count;
    }

    for (i = count - n; i < count; i++) {
        sum += g_scope.samples[i];
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
        int32_t v = (int32_t)g_scope.samples[i] - offset + 2048;
        if (v < 0) {
            v = 0;
        } else if (v > 4095) {
            v = 4095;
        }
        g_scope.ac_coupled[i] = (uint16_t)v;
    }

    return g_scope.ac_coupled;
}

/* ═══ Measurements ═══ */

/* ADC full scale 4095 corresponds to ~3.3V (with attenuation), per LSB ~ 0.805 mV */
#define SCOPE_MV_PER_LSB_NUM 805
#define SCOPE_MV_PER_LSB_DEN 1000

static uint16_t scope_raw_to_mv(uint16_t raw)
{
    uint32_t mv = ((uint32_t)raw * SCOPE_MV_PER_LSB_NUM) / SCOPE_MV_PER_LSB_DEN;
    if (mv > 9999U) {
        mv = 9999U;
    }
    return (uint16_t)mv;
}

static void scope_update_measurements(const uint16_t *buf, uint16_t count)
{
    if (buf == NULL || count == 0U) {
        g_scope.view.vpp_raw = 0;
        g_scope.view.vavg_raw = 0;
        g_scope.view.vpp_mv = 0;
        g_scope.view.vavg_mv = 0;
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
    g_scope.view.vpp_mv = scope_raw_to_mv(g_scope.view.vpp_raw);
    g_scope.view.vavg_mv = scope_raw_to_mv(g_scope.view.vavg_raw);

    uint32_t sr = g_scope.view.sample_rate_hz;
    if (sr == 0) {
        g_scope.view.freq_hz = 0;
        return;
    }

    uint16_t avg = g_scope.view.vavg_raw;
    uint16_t hysteresis = (maxv - minv) / 20U;  /* 5% of Vpp hysteresis */
    if (hysteresis == 0U) {
        hysteresis = 5U;
    }
    uint16_t crossings = 0;
    bool was_above = (buf[0] >= avg + hysteresis);

    for (uint16_t i = 1; i < count; i++) {
        bool is_above = (buf[i] >= avg + hysteresis);
        bool is_below = (buf[i] <= avg - hysteresis);
        if (was_above && is_below) {
            crossings++;
            was_above = false;
        } else if (!was_above && is_above) {
            crossings++;
            was_above = true;
        }
    }

    g_scope.view.freq_hz = ((uint32_t)crossings * sr) / (2U * count);
}

/* ═══ Lifecycle ═══ */

void oscilloscope_init(void *user_data)
{
    (void)user_data;
    memset(&g_scope, 0, sizeof(g_scope));

    g_scope.view.samples = g_scope.samples;
    g_scope.view.sample_count = SCOPE_SAMPLE_MAX;
    g_scope.view.trigger_index = 0xFFFF;
    g_scope.view.running = true;
    g_scope.view.selected_param = PARAM_TIME_BASE;
    g_scope.view.trigger_level = 2048; /* mid-scale */
    g_scope.view.sample_rate_index = 3; /* 20kHz */
    g_scope.view.filter_index = 1;      /* Low EMA */
    g_scope.last_filtered = 2048;
    scope_sync_sample_rate();

#ifndef NATIVE_TEST
    /* Configure GPIO36 as input (no pull, analog pin) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SCOPE_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Initialize ADC oneshot unit */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_cfg, &s_adc_handle);

    /* Configure ADC channel with 11dB attenuation (~3.3V full scale) */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(s_adc_handle, SCOPE_ADC_CHANNEL, &chan_cfg);
#else
    pinMode(SCOPE_PIN, INPUT);
    analogSetPinAttenuation(SCOPE_PIN, ADC_11db);
#endif

    ui_service_enter_landscape();
    ui_service_user_item_init();
}

void oscilloscope_exit(void *user_data)
{
    (void)user_data;

#ifndef NATIVE_TEST
    if (s_adc_handle != NULL) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
#endif

    ui_service_user_item_exit();
    ui_service_exit_landscape();
}

/* ═══ Main Loop ═══ */

static void scope_delay_us(uint32_t us)
{
#ifndef NATIVE_TEST
    uint64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < (uint64_t)us) {
        /* Busy-wait for microsecond precision */
    }
#else
    delayMicroseconds(us);
#endif
}

void oscilloscope_loop(void *user_data)
{
    (void)user_data;

    if (g_scope.view.running) {
        oscilloscope_ui_mark_dirty();
        uint32_t div_us = g_scope_time_bases[g_scope.view.time_base_index].time_per_div_us;
        uint32_t sr      = g_scope.view.sample_rate_hz;
        uint16_t samples_to_take = SCOPE_SAMPLE_MAX;
        if (sr != 0U) {
            uint32_t window_us = div_us * 10U;   /* 10 horizontal divisions on screen */
            uint32_t need = (window_us * sr) / 1000000UL;
            if (need == 0U) {
                need = 1U;
            }
            if (need < SCOPE_SAMPLE_MAX) {
                samples_to_take = (uint16_t)need;
            }
        }

        uint32_t period_us = (sr == 0U) ? 0U : (1000000UL / sr);
        const uint32_t adc_overhead_us = 20U;

        for (uint16_t i = 0; i < samples_to_take; i++) {
            scope_sample_one(i);
            if (period_us > adc_overhead_us + 5U) {
                scope_delay_us((uint32_t)(period_us - adc_overhead_us));
            }
        }
        g_scope.view.sample_count = samples_to_take;
        scope_update_trigger();
    }

    oscilloscope_ui_draw(&g_scope.view);

    hal_event_t ev_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t ev_b = hal_input_get_event(HAL_BTN_B);

    scope_handle_input(ev_a, ev_b);

    if (ui_service_user_item_loop(ev_b)) {
        return;
    }
}
