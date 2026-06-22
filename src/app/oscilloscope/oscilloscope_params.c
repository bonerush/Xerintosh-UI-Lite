/**
 * @file   oscilloscope_params.c
 * @brief  Oscilloscope parameter table definitions (time base / voltage range / coupling / trigger mode / sample rate / filter)
 * @details Constant tables for UI layer, engine layer, and test code. Split from oscilloscope_app.c.
 *
 * @copyright Copyright (c) 2026
 */

#include "oscilloscope_engine.h"

/* ═══ Parameter Tables ═══ */

/* Screen horizontal ~10 divisions, time_per_div_us is time per division.
 * Total window time = time_per_div_us * 10; at fixed SR, required samples = window * SR. */
const scope_time_base_t g_scope_time_bases[SCOPE_TIME_BASE_COUNT] = {
    { "50us",  50 },
    { "100us", 100 },
    { "200us", 200 },
    { "500us", 500 },
    { "1ms",   1000 },
    { "2ms",   2000 },
    { "5ms",   5000 },
};

const scope_volt_range_t g_scope_volt_ranges[SCOPE_VOLT_RANGE_COUNT] = {
    { "0.5V",  620 },
    { "1V",   1241 },
    { "2V",   2482 },
    { "3.3V", 4095 },
};

const char *g_scope_coupling_labels[SCOPE_COUPLING_COUNT] = { "DC", "AC", "GND" };

const char *g_scope_trigger_mode_labels[SCOPE_TRIGGER_MODE_COUNT] = { "Auto", "Norm", "Scan" };

/* ADC oneshot read busy-wait ~30-90us; polling mode max ~11kHz.
 * 20kHz is a reasonable upper limit for polling mode. */
const scope_sample_rate_t g_scope_sample_rates[SCOPE_SAMPLE_RATE_COUNT] = {
    { "1kHz",   1000 },
    { "5kHz",   5000 },
    { "10kHz", 10000 },
    { "20kHz", 20000 },
};

/* prev_weight: historical value weight, current raw value weight = 8 - prev_weight
 * Low=weak filter, Med=medium, Hi=strong filter */
const scope_filter_t g_scope_filters[SCOPE_FILTER_COUNT] = {
    { "Off", 0 },
    { "Low", 2 },
    { "Med", 4 },
    { "Hi",  6 },
};
