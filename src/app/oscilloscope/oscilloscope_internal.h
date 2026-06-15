#ifndef OSCILLOSCOPE_INTERNAL_H
#define OSCILLOSCOPE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "oscilloscope_ui.h"

#define SCOPE_SAMPLE_MAX 2000

typedef struct {
    uint16_t samples[SCOPE_SAMPLE_MAX];
    uint16_t ac_coupled[SCOPE_SAMPLE_MAX];
    oscilloscope_view_state_t view;
    int16_t  ac_offset;
    uint16_t last_filtered;
} scope_state_t;

extern scope_state_t g_scope;

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_INTERNAL_H */
