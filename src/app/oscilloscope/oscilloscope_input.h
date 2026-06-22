#ifndef OSCILLOSCOPE_INPUT_H
#define OSCILLOSCOPE_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "hal/hal_input.h"

void scope_sync_sample_rate(void);
void scope_handle_input(hal_event_t ev_a, hal_event_t ev_b);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_INPUT_H */
