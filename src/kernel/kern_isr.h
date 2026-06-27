#ifndef KERN_ISR_H
#define KERN_ISR_H

#include "kern_types.h"
#include "kern_ipc.h"
#include "kern_task.h"
#include "kern_task_notify.h"

#ifdef __cplusplus
extern "C" {
#endif

kern_err_t kern_bin_sem_give_from_isr(kern_bin_sem_t *sem,
                                       bool *higher_prio_task_woken);
kern_err_t kern_sem_give_from_isr(kern_sem_t *sem,
                                   bool *higher_prio_task_woken);
kern_err_t kern_event_set_from_isr(kern_event_t *ev, uint32_t bits,
                                    bool *higher_prio_task_woken);
kern_err_t kern_task_notify_from_isr(kern_task_t *task, uint32_t value,
                                      kern_notify_action_t action,
                                      bool *higher_prio_task_woken);

void kern_yield_from_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_ISR_H */
