#ifndef OSCILLOSCOPE_UI_H
#define OSCILLOSCOPE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define SCOPE_TIME_BASE_COUNT 7
#define SCOPE_VOLT_RANGE_COUNT 4
#define SCOPE_COUPLING_COUNT 3
#define SCOPE_TRIGGER_MODE_COUNT 3
#define SCOPE_AC_OFFSET_WINDOW 32

typedef enum {
    SCOPE_TRIGGER_AUTO = 0,
    SCOPE_TRIGGER_NORMAL,
    SCOPE_TRIGGER_SCAN
} scope_trigger_mode_t;

typedef struct {
    const uint16_t *samples;
    uint16_t sample_count;
    uint16_t trigger_index;
    uint32_t sample_rate_hz;
    uint8_t time_base_index;
    uint8_t volt_range_index;
    uint8_t coupling_index;
    uint8_t trigger_mode_index;
    int16_t trigger_level;
    bool running;
    bool editing;
    uint8_t selected_param;
    uint16_t vpp_raw;
    uint16_t vavg_raw;
    uint32_t freq_hz;
} oscilloscope_view_state_t;

typedef enum {
    PARAM_TIME_BASE = 0,
    PARAM_VOLT_RANGE,
    PARAM_COUPLING,
    PARAM_TRIGGER_MODE,
    PARAM_TRIGGER_LEVEL,
    PARAM_COUNT
} scope_param_t;

void oscilloscope_ui_draw(const oscilloscope_view_state_t *state);

/* ═══ 参数表（引擎导出，供 UI 与测试使用）═══ */

typedef struct {
    const char *label;
    uint8_t     samples_per_pixel;
    uint32_t    display_rate_hz;
} scope_time_base_t;

extern const scope_time_base_t g_scope_time_bases[SCOPE_TIME_BASE_COUNT];

typedef struct {
    const char *label;
    uint16_t    full_scale;
} scope_volt_range_t;

extern const scope_volt_range_t g_scope_volt_ranges[SCOPE_VOLT_RANGE_COUNT];

extern const char *g_scope_coupling_labels[SCOPE_COUPLING_COUNT];

extern const char *g_scope_trigger_mode_labels[SCOPE_TRIGGER_MODE_COUNT];

/* ═══ 触发查找（供测试直接调用）═══ */

uint16_t scope_find_trigger_rising(uint16_t start, uint16_t level);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_UI_H */
