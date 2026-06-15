#ifndef OSCILLOSCOPE_UI_H
#define OSCILLOSCOPE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "oscilloscope_engine.h"

typedef enum {
    PARAM_TIME_BASE = 0,
    PARAM_VOLT_RANGE,
    PARAM_COUPLING,
    PARAM_TRIGGER_MODE,
    PARAM_TRIGGER_LEVEL,
    PARAM_SAMPLE_RATE,
    PARAM_FILTER,
    PARAM_COUNT
} scope_param_t;

typedef struct {
    const uint16_t *samples;
    uint16_t sample_count;
    uint16_t trigger_index;
    uint32_t sample_rate_hz;
    uint8_t time_base_index;
    uint8_t volt_range_index;
    uint8_t coupling_index;
    uint8_t trigger_mode_index;
    uint8_t sample_rate_index;
    uint8_t filter_index;
    int16_t trigger_level;
    bool running;
    bool editing;
    uint8_t selected_param;
    uint16_t vpp_raw;
    uint16_t vavg_raw;
    uint32_t freq_hz;
    uint16_t vpp_mv;
    uint16_t vavg_mv;
} oscilloscope_view_state_t;

void oscilloscope_ui_draw(const oscilloscope_view_state_t *state);
void oscilloscope_ui_mark_dirty(void);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_UI_H */
