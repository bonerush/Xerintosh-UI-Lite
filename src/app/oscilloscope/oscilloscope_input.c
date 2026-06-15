#include "oscilloscope_input.h"
#include "oscilloscope_internal.h"
#include "oscilloscope_engine.h"

#include "hal/hal_input.h"

#define SCOPE_TRIGGER_LEVEL_STEP 100

static void scope_param_next(void)
{
    g_scope.view.selected_param++;
    if (g_scope.view.selected_param >= PARAM_COUNT) {
        g_scope.view.selected_param = 0;
    }
}

static void scope_param_prev(void)
{
    if (g_scope.view.selected_param == 0) {
        g_scope.view.selected_param = PARAM_COUNT - 1;
    } else {
        g_scope.view.selected_param--;
    }
}

static void scope_adjust_index(uint8_t *idx, uint8_t count, int8_t dir, bool wrap)
{
    if (dir > 0) {
        if (wrap) {
            *idx = (*idx + 1U) % count;
        } else if (*idx + 1U < count) {
            (*idx)++;
        }
    } else {
        if (wrap) {
            *idx = (*idx + count - 1U) % count;
        } else if (*idx > 0U) {
            (*idx)--;
        }
    }
}

static void scope_param_adjust(int8_t dir)
{
    switch (g_scope.view.selected_param) {
    case PARAM_TIME_BASE:
        scope_adjust_index(&g_scope.view.time_base_index, SCOPE_TIME_BASE_COUNT, dir, false);
        break;
    case PARAM_VOLT_RANGE:
        scope_adjust_index(&g_scope.view.volt_range_index, SCOPE_VOLT_RANGE_COUNT, dir, false);
        break;
    case PARAM_COUPLING:
        scope_adjust_index(&g_scope.view.coupling_index, SCOPE_COUPLING_COUNT, dir, true);
        break;
    case PARAM_TRIGGER_MODE:
        scope_adjust_index(&g_scope.view.trigger_mode_index, SCOPE_TRIGGER_MODE_COUNT, dir, true);
        break;
    case PARAM_TRIGGER_LEVEL:
        if (dir > 0) {
            if (g_scope.view.trigger_level < 4095 - SCOPE_TRIGGER_LEVEL_STEP) {
                g_scope.view.trigger_level += SCOPE_TRIGGER_LEVEL_STEP;
            } else {
                g_scope.view.trigger_level = 4095;
            }
        } else {
            if (g_scope.view.trigger_level > SCOPE_TRIGGER_LEVEL_STEP) {
                g_scope.view.trigger_level -= SCOPE_TRIGGER_LEVEL_STEP;
            } else {
                g_scope.view.trigger_level = 0;
            }
        }
        break;
    case PARAM_SAMPLE_RATE:
        scope_adjust_index(&g_scope.view.sample_rate_index, SCOPE_SAMPLE_RATE_COUNT, dir, false);
        break;
    case PARAM_FILTER:
        scope_adjust_index(&g_scope.view.filter_index, SCOPE_FILTER_COUNT, dir, false);
        break;
    default:
        break;
    }
}

void scope_sync_sample_rate(void)
{
    uint8_t idx = g_scope.view.sample_rate_index;
    if (idx >= SCOPE_SAMPLE_RATE_COUNT) {
        idx = 0;
    }
    g_scope.view.sample_rate_hz = g_scope_sample_rates[idx].rate_hz;
}

void scope_handle_input(hal_event_t ev_a, hal_event_t ev_b)
{
    if (!g_scope.view.editing) {
        if (ev_a == HAL_EVENT_SHORT_PRESS) {
            scope_param_next();
        } else if (ev_a == HAL_EVENT_LONG_PRESS) {
            g_scope.view.editing = true;
            hal_input_reset_events();
        }
        if (ev_b == HAL_EVENT_SHORT_PRESS) {
            g_scope.view.running = !g_scope.view.running;
        }
        return;
    }

    /* editing mode: A=increase, B=decrease (consistent with framework slider) */
    if (ev_a == HAL_EVENT_SHORT_PRESS) {
        scope_param_adjust(1);
        scope_sync_sample_rate();
    } else if (ev_a == HAL_EVENT_LONG_PRESS) {
        g_scope.view.editing = false;
        hal_input_reset_events();
    }
    if (ev_b == HAL_EVENT_SHORT_PRESS) {
        scope_param_adjust(-1);
        scope_sync_sample_rate();
    } else if (ev_b == HAL_EVENT_LONG_PRESS) {
        /* cancel editing first, then the next B long press exits the app */
        g_scope.view.editing = false;
        hal_input_reset_events();
    }
}
