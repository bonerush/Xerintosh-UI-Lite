#include "oscilloscope_input.h"
#include "oscilloscope_internal.h"
#include "oscilloscope_engine.h"

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

static void scope_param_decrease(void)
{
    switch (g_scope.view.selected_param) {
    case PARAM_TIME_BASE:
        if (g_scope.view.time_base_index > 0) {
            g_scope.view.time_base_index--;
        }
        break;
    case PARAM_VOLT_RANGE:
        if (g_scope.view.volt_range_index > 0) {
            g_scope.view.volt_range_index--;
        }
        break;
    case PARAM_COUPLING:
        g_scope.view.coupling_index =
            (g_scope.view.coupling_index + SCOPE_COUPLING_COUNT - 1) % SCOPE_COUPLING_COUNT;
        break;
    case PARAM_TRIGGER_MODE:
        g_scope.view.trigger_mode_index =
            (g_scope.view.trigger_mode_index + SCOPE_TRIGGER_MODE_COUNT - 1) % SCOPE_TRIGGER_MODE_COUNT;
        break;
    case PARAM_TRIGGER_LEVEL:
        if (g_scope.view.trigger_level > SCOPE_TRIGGER_LEVEL_STEP) {
            g_scope.view.trigger_level -= SCOPE_TRIGGER_LEVEL_STEP;
        } else {
            g_scope.view.trigger_level = 0;
        }
        break;
    default:
        break;
    }
}

static void scope_param_increase(void)
{
    switch (g_scope.view.selected_param) {
    case PARAM_TIME_BASE:
        if (g_scope.view.time_base_index + 1 < SCOPE_TIME_BASE_COUNT) {
            g_scope.view.time_base_index++;
        }
        break;
    case PARAM_VOLT_RANGE:
        if (g_scope.view.volt_range_index + 1 < SCOPE_VOLT_RANGE_COUNT) {
            g_scope.view.volt_range_index++;
        }
        break;
    case PARAM_COUPLING:
        g_scope.view.coupling_index =
            (g_scope.view.coupling_index + 1) % SCOPE_COUPLING_COUNT;
        break;
    case PARAM_TRIGGER_MODE:
        g_scope.view.trigger_mode_index =
            (g_scope.view.trigger_mode_index + 1) % SCOPE_TRIGGER_MODE_COUNT;
        break;
    case PARAM_TRIGGER_LEVEL:
        if (g_scope.view.trigger_level < 4095 - SCOPE_TRIGGER_LEVEL_STEP) {
            g_scope.view.trigger_level += SCOPE_TRIGGER_LEVEL_STEP;
        } else {
            g_scope.view.trigger_level = 4095;
        }
        break;
    default:
        break;
    }
}

void scope_sync_time_base(void)
{
    g_scope.view.sample_rate_hz =
        g_scope_time_bases[g_scope.view.time_base_index].display_rate_hz;
}

void scope_handle_input(hal_event_t ev_a, hal_event_t ev_b)
{
    if (!g_scope.view.editing) {
        if (ev_a == HAL_EVENT_SHORT_PRESS) {
            scope_param_next();
        } else if (ev_a == HAL_EVENT_LONG_PRESS) {
            g_scope.view.editing = true;
        }
        if (ev_b == HAL_EVENT_SHORT_PRESS) {
            g_scope.view.running = !g_scope.view.running;
        }
        return;
    }

    if (ev_a == HAL_EVENT_SHORT_PRESS) {
        scope_param_decrease();
        scope_sync_time_base();
    } else if (ev_a == HAL_EVENT_LONG_PRESS) {
        g_scope.view.editing = false;
    }
    if (ev_b == HAL_EVENT_SHORT_PRESS) {
        scope_param_increase();
        scope_sync_time_base();
    }
    /* BtnB long press is reserved by the framework for app exit. */
}
