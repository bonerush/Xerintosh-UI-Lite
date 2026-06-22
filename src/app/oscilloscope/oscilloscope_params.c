/**
 * @file   oscilloscope_params.c
 * @brief  示波器参数表定义（时基/电压档/耦合/触发模式/采样率/滤波器）
 * @details 常量表供 UI 层、引擎层和测试代码使用。从 oscilloscope_app.c 拆分。
 *
 * @copyright Copyright (c) 2026
 */

#include "oscilloscope_engine.h"

/* ═══ 参数表 ═══ */

/* 屏幕横向约 10 格，time_per_div_us 为每格代表的时间。
 * 总窗口时间 = time_per_div_us * 10；在固定 SR 下所需样本数 = 窗口 * SR。 */
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

/* analogRead 忙等约 90µs，轮询模式下最大可达 ~11kHz。
 * 20kHz 是 Polling 模式下的合理上限 */
const scope_sample_rate_t g_scope_sample_rates[SCOPE_SAMPLE_RATE_COUNT] = {
    { "1kHz",   1000 },
    { "5kHz",   5000 },
    { "10kHz", 10000 },
    { "20kHz", 20000 },
};

/* prev_weight: 历史值权重，当前原始值权重 = 8 - prev_weight
 * Low=弱滤波, Med=中, Hi=强滤波 */
const scope_filter_t g_scope_filters[SCOPE_FILTER_COUNT] = {
    { "Off", 0 },
    { "Low", 2 },
    { "Med", 4 },
    { "Hi",  6 },
};
