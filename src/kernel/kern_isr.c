#include "kern_isr.h"
#include "kern_sched.h"
#include "kern_smp.h"

static void isr_note_woken(kern_task_t *woken, bool *higher_prio_task_woken)
{
    if (higher_prio_task_woken == NULL || woken == NULL) return;

    kern_task_t *current = kern_task_current();
    if (current != NULL && woken->priority > current->priority) {
        *higher_prio_task_woken = true;
    }
}

kern_err_t kern_bin_sem_give_from_isr(kern_bin_sem_t *sem,
                                       bool *higher_prio_task_woken)
{
    if (sem == NULL) return KERN_EINVAL;
    if (higher_prio_task_woken != NULL) *higher_prio_task_woken = false;

    xeros_spinlock_lock(&sem->lock);
    if (sem->count < 1) {
        sem->count = 1;
        kern_task_t *woken = ipc_wait_dequeue(&sem->wait_queue);
        if (woken != NULL) {
            woken->state = KERN_TASK_READY;
            isr_note_woken(woken, higher_prio_task_woken);
            kern_smp_ipi_reschedule(woken->cpu_id);
        }
    }
    xeros_spinlock_unlock(&sem->lock);
    return KERN_OK;
}

kern_err_t kern_sem_give_from_isr(kern_sem_t *sem,
                                   bool *higher_prio_task_woken)
{
    if (sem == NULL) return KERN_EINVAL;
    if (higher_prio_task_woken != NULL) *higher_prio_task_woken = false;

    xeros_spinlock_lock(&sem->lock);
    if (sem->count < sem->max_count) {
        sem->count++;
        kern_task_t *woken = ipc_wait_dequeue(&sem->wait_queue);
        if (woken != NULL) {
            woken->state = KERN_TASK_READY;
            isr_note_woken(woken, higher_prio_task_woken);
            kern_smp_ipi_reschedule(woken->cpu_id);
        }
    }
    xeros_spinlock_unlock(&sem->lock);
    return KERN_OK;
}

kern_err_t kern_event_set_from_isr(kern_event_t *ev, uint32_t bits,
                                    bool *higher_prio_task_woken)
{
    if (ev == NULL) return KERN_EINVAL;
    if (higher_prio_task_woken != NULL) *higher_prio_task_woken = false;

    xeros_spinlock_lock(&ev->lock);
    ev->bits |= bits;

    kern_wait_node_t **pp = &ev->wait_queue;
    while (*pp != NULL) {
        kern_wait_node_t *node = *pp;
        kern_task_t *woken = node->task;
        *pp = node->next;
        node->next = NULL;
        if (woken != NULL) {
            woken->state = KERN_TASK_READY;
            isr_note_woken(woken, higher_prio_task_woken);
            kern_smp_ipi_reschedule(woken->cpu_id);
        }
    }

    xeros_spinlock_unlock(&ev->lock);
    return KERN_OK;
}

kern_err_t kern_task_notify_from_isr(kern_task_t *task, uint32_t value,
                                      kern_notify_action_t action,
                                      bool *higher_prio_task_woken)
{
    if (task == NULL) return KERN_EINVAL;
    if (higher_prio_task_woken != NULL) *higher_prio_task_woken = false;

    kern_err_t rc = kern_task_notify(task, value, action);
    if (rc == KERN_OK) {
        isr_note_woken(task, higher_prio_task_woken);
    }
    return rc;
}

void kern_yield_from_isr(void)
{
    kern_set_need_resched(true);
}
