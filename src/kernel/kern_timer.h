#ifndef KERN_TIMER_H
#define KERN_TIMER_H

#include "kern_types.h"
#include "kern_task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KERN_TIMER_ONCE = 0,
    KERN_TIMER_AUTORELOAD = 1
} kern_timer_mode_t;

typedef void (*kern_timer_callback_t)(void *arg);

typedef struct kern_timer {
    char                name[KERN_TASK_NAME_LEN + 1];
    uint32_t            period_ms;
    uint32_t            expiry;
    kern_timer_mode_t   mode;
    kern_timer_callback_t callback;
    void               *arg;
    bool                active;
    struct kern_timer  *next;
} kern_timer_t;

kern_err_t kern_timer_init(void);
kern_err_t kern_timer_create(kern_timer_t *timer, const char *name,
                              kern_timer_callback_t cb, void *arg,
                              uint32_t period_ms, kern_timer_mode_t mode);
kern_err_t kern_timer_start(kern_timer_t *timer);
kern_err_t kern_timer_stop(kern_timer_t *timer);
kern_err_t kern_timer_reset(kern_timer_t *timer);
void       kern_timer_process(void);
void       kern_timer_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TIMER_H */
