#ifndef KERN_STATS_H
#define KERN_STATS_H

#include "kern_types.h"
#include "kern_task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KERN_WATCHDOG_TIMEOUT_MS 5000

void     kern_stats_init(void);
void     kern_stats_task_start(kern_task_t *task);
void     kern_stats_task_stop(kern_task_t *task);
void     kern_stats_update(void);
uint64_t kern_stats_get_runtime_us(const kern_task_t *task);
uint8_t  kern_stats_get_cpu_percent(const kern_task_t *task);

kern_err_t kern_watchdog_register(kern_task_t *task);
kern_err_t kern_watchdog_feed(kern_task_t *task);
void       kern_watchdog_check(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_STATS_H */
