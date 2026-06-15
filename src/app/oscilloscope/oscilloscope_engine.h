#ifndef OSCILLOSCOPE_ENGINE_H
#define OSCILLOSCOPE_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SCOPE_TIME_BASE_COUNT 7
#define SCOPE_VOLT_RANGE_COUNT 4
#define SCOPE_COUPLING_COUNT 3
#define SCOPE_TRIGGER_MODE_COUNT 3
#define SCOPE_SAMPLE_RATE_COUNT 4
#define SCOPE_FILTER_COUNT 4

typedef enum {
    SCOPE_TRIGGER_AUTO = 0,
    SCOPE_TRIGGER_NORMAL,
    SCOPE_TRIGGER_SCAN
} scope_trigger_mode_t;

typedef struct {
    const char *label;
    uint32_t time_per_div_us;   /* 每格时间，屏幕共 10 格 */
} scope_time_base_t;

extern const scope_time_base_t g_scope_time_bases[SCOPE_TIME_BASE_COUNT];

typedef struct {
    const char *label;
    uint16_t full_scale;
} scope_volt_range_t;

extern const scope_volt_range_t g_scope_volt_ranges[SCOPE_VOLT_RANGE_COUNT];
extern const char *g_scope_coupling_labels[SCOPE_COUPLING_COUNT];
extern const char *g_scope_trigger_mode_labels[SCOPE_TRIGGER_MODE_COUNT];

typedef struct {
    const char *label;
    uint32_t rate_hz;
} scope_sample_rate_t;

extern const scope_sample_rate_t g_scope_sample_rates[SCOPE_SAMPLE_RATE_COUNT];

typedef struct {
    const char *label;
    uint8_t prev_weight;   /* 历史值权重，当前值权重 = 8 - prev_weight */
} scope_filter_t;

extern const scope_filter_t g_scope_filters[SCOPE_FILTER_COUNT];

uint16_t scope_find_trigger_rising(const uint16_t *buf, uint16_t count,
                                   uint16_t start, uint16_t level);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_ENGINE_H */
