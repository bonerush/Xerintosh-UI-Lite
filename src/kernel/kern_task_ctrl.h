#ifndef KERN_TASK_CTRL_H
#define KERN_TASK_CTRL_H

#include "kern_types.h"
#include "kern_task.h"

#ifdef __cplusplus
extern "C" {
#endif

kern_err_t kern_task_suspend(kern_task_t *task);
kern_err_t kern_task_resume(kern_task_t *task);

uint8_t    kern_task_priority_get(const kern_task_t *task);
kern_err_t kern_task_priority_set(kern_task_t *task, uint8_t new_prio);

void       kern_task_delay(uint32_t ms);
void       kern_task_delay_until(uint32_t *prev_wake_time, uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TASK_CTRL_H */
