#include "kern_task_ctrl.h"
#include "kern_sched.h"
#include "kern_sched_class.h"
#include "kern_smp.h"

kern_err_t kern_task_suspend(kern_task_t *task)
{
    if (task == NULL) task = kern_task_current();
    if (task == NULL) return KERN_EINVAL;
    if (task == g_idle_task) return KERN_EPERM;

    task->state = KERN_TASK_SUSPENDED;
    if (task == kern_task_current()) {
        kern_yield();
    }
    return KERN_OK;
}

kern_err_t kern_task_resume(kern_task_t *task)
{
    if (task == NULL) return KERN_EINVAL;
    if (task->state == KERN_TASK_SUSPENDED) {
        task->state = KERN_TASK_READY;
        kern_smp_ipi_reschedule(task->cpu_id);
    }
    return KERN_OK;
}

uint8_t kern_task_priority_get(const kern_task_t *task)
{
    if (task == NULL) task = kern_task_current();
    if (task == NULL) return 0;
    return task->priority;
}

kern_err_t kern_task_priority_set(kern_task_t *task, uint8_t new_prio)
{
    if (task == NULL) task = kern_task_current();
    if (task == NULL) return KERN_EINVAL;

    uint8_t old = task->priority;
    task->priority = new_prio;
    task->base_priority = new_prio;

    for (int i = 0; i < g_sched_class_count; i++) {
        kern_sched_class_t *cls = g_sched_classes[i];
        if (cls != NULL && cls->prio_changed != NULL) {
            cls->prio_changed(task, old);
        }
    }

    if (new_prio > old && task != kern_task_current()) {
        kern_set_need_resched(true);
        kern_smp_ipi_reschedule(task->cpu_id);
    }

    return KERN_OK;
}

void kern_task_delay(uint32_t ms)
{
    kern_sleep_ms(ms);
}

void kern_task_delay_until(uint32_t *prev_wake_time, uint32_t ms)
{
    if (prev_wake_time == NULL) return;

    uint32_t next = *prev_wake_time + ms;
    int32_t remain = (int32_t)(next - g_sched_ticks);

    if (remain > 0) {
        kern_sleep_ms((uint32_t)remain);
    }

    *prev_wake_time = next;
}
