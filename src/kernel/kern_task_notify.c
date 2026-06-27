#include "kern_task_notify.h"
#include "kern_sched.h"
#include "kern_smp.h"
#include "kern_sync.h"

#include <string.h>

static xeros_spinlock_t s_notify_lock;
static bool s_notify_inited = false;

kern_err_t kern_task_notify_init(void)
{
    if (s_notify_inited) return KERN_OK;
    xeros_spinlock_init(&s_notify_lock);
    s_notify_inited = true;
    return KERN_OK;
}

static void notify_wake_locked(kern_task_t *task)
{
    task->notify_state = KERN_NOTIFY_RECEIVED;
    if (task->state == KERN_TASK_SLEEPING) {
        task->state = KERN_TASK_READY;
    }
    kern_smp_ipi_reschedule(task->cpu_id);
}

kern_err_t kern_task_notify_give(kern_task_t *task)
{
    if (task == NULL) return KERN_EINVAL;

    xeros_spinlock_lock(&s_notify_lock);
    task->notify_value++;
    notify_wake_locked(task);
    xeros_spinlock_unlock(&s_notify_lock);
    return KERN_OK;
}

kern_err_t kern_task_notify(kern_task_t *task, uint32_t value,
                            kern_notify_action_t action)
{
    if (task == NULL) return KERN_EINVAL;

    xeros_spinlock_lock(&s_notify_lock);

    switch (action) {
    case KERN_NOTIFY_NO_ACTION:
        break;
    case KERN_NOTIFY_SET_BITS:
        task->notify_value |= value;
        break;
    case KERN_NOTIFY_INCREMENT:
        task->notify_value++;
        break;
    case KERN_NOTIFY_SET_VALUE_WITH_OVERWRITE:
        task->notify_value = value;
        break;
    case KERN_NOTIFY_SET_VALUE_WITHOUT_OVERWRITE:
        if (task->notify_state != KERN_NOTIFY_WAITING) {
            xeros_spinlock_unlock(&s_notify_lock);
            return KERN_EAGAIN;
        }
        task->notify_value = value;
        break;
    default:
        xeros_spinlock_unlock(&s_notify_lock);
        return KERN_EINVAL;
    }

    notify_wake_locked(task);
    xeros_spinlock_unlock(&s_notify_lock);
    return KERN_OK;
}

bool kern_task_notify_take(bool clear_on_exit, uint32_t timeout_ms)
{
    kern_task_t *cur = kern_task_current();
    if (cur == NULL) return false;

    xeros_spinlock_lock(&s_notify_lock);
    if (cur->notify_value > 0) {
        bool taken = true;
        if (clear_on_exit) {
            cur->notify_value = 0;
        } else {
            cur->notify_value--;
        }
        cur->notify_state = KERN_NOTIFY_NOT_WAITING;
        xeros_spinlock_unlock(&s_notify_lock);
        return taken;
    }

    if (timeout_ms == 0) {
        cur->notify_state = KERN_NOTIFY_NOT_WAITING;
        xeros_spinlock_unlock(&s_notify_lock);
        return false;
    }

    cur->notify_state = KERN_NOTIFY_WAITING;
    xeros_spinlock_unlock(&s_notify_lock);

    kern_sleep_ms(timeout_ms);

    xeros_spinlock_lock(&s_notify_lock);
    bool taken = (cur->notify_state == KERN_NOTIFY_RECEIVED && cur->notify_value > 0);
    if (taken) {
        if (clear_on_exit) {
            cur->notify_value = 0;
        } else {
            cur->notify_value--;
        }
    }
    cur->notify_state = KERN_NOTIFY_NOT_WAITING;
    xeros_spinlock_unlock(&s_notify_lock);
    return taken;
}

uint32_t kern_task_notify_wait_bits(uint32_t bits_to_wait, bool clear_on_exit,
                                    bool wait_all, uint32_t timeout_ms)
{
    kern_task_t *cur = kern_task_current();
    if (cur == NULL) return 0;

    xeros_spinlock_lock(&s_notify_lock);
    uint32_t current = cur->notify_value;
    bool satisfied = wait_all
        ? ((current & bits_to_wait) == bits_to_wait)
        : ((current & bits_to_wait) != 0);

    if (satisfied) {
        uint32_t ret = current;
        if (clear_on_exit) {
            cur->notify_value &= ~bits_to_wait;
        }
        cur->notify_state = KERN_NOTIFY_NOT_WAITING;
        xeros_spinlock_unlock(&s_notify_lock);
        return ret;
    }

    if (timeout_ms == 0) {
        cur->notify_state = KERN_NOTIFY_NOT_WAITING;
        xeros_spinlock_unlock(&s_notify_lock);
        return 0;
    }

    cur->notify_state = KERN_NOTIFY_WAITING;
    xeros_spinlock_unlock(&s_notify_lock);

    kern_sleep_ms(timeout_ms);

    xeros_spinlock_lock(&s_notify_lock);
    current = cur->notify_value;
    satisfied = wait_all
        ? ((current & bits_to_wait) == bits_to_wait)
        : ((current & bits_to_wait) != 0);

    uint32_t ret = 0;
    if (satisfied) {
        ret = current;
        if (clear_on_exit) {
            cur->notify_value &= ~bits_to_wait;
        }
    }
    cur->notify_state = KERN_NOTIFY_NOT_WAITING;
    xeros_spinlock_unlock(&s_notify_lock);
    return ret;
}
