/**
 * @file   kern_task_notify.c
 * @brief  Xeros 任务通知核心实现
 * @details 实现任务通知的发送、接收、位等待等操作。
 *          使用自旋锁保护 TCB 中的通知状态与值，并通过原子状态机避免丢失唤醒。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_task_notify.h"
#include "kern_sched.h"
#include "kern_smp.h"
#include "kern_sync.h"
#include "kern_critical.h"

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
#ifdef CONFIG_SMP_ENABLED
        kern_smp_ipi_reschedule(task->cpu_id);
#endif
    }
}

kern_err_t kern_task_notify_give(kern_task_t *task)
{
    if (!s_notify_inited) return KERN_EAGAIN;
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
    if (!s_notify_inited) return KERN_EAGAIN;
    if (task == NULL) return KERN_EINVAL;

    uint32_t irq_state = kern_enter_critical();
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
            kern_exit_critical(irq_state);
            return KERN_EAGAIN;
        }
        task->notify_value = value;
        break;
    default:
        xeros_spinlock_unlock(&s_notify_lock);
        kern_exit_critical(irq_state);
        return KERN_EINVAL;
    }

    notify_wake_locked(task);
    xeros_spinlock_unlock(&s_notify_lock);
    kern_exit_critical(irq_state);
    return KERN_OK;
}

bool kern_task_notify_take(bool clear_on_exit, uint32_t timeout_ms)
{
    if (!s_notify_inited) return false;

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

    /* 使用短睡眠 + 重试循环，避免 unlock -> sleep 之间丢失唤醒。
     * 每次循环重新检查 notify_value，确保在通知到达时立即返回。 */
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        kern_sleep_ms(1);
        elapsed++;

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
        xeros_spinlock_unlock(&s_notify_lock);
    }

    xeros_spinlock_lock(&s_notify_lock);
    cur->notify_state = KERN_NOTIFY_NOT_WAITING;
    xeros_spinlock_unlock(&s_notify_lock);
    return false;
}

uint32_t kern_task_notify_wait_bits(uint32_t bits_to_wait, bool clear_on_exit,
                                    bool wait_all, uint32_t timeout_ms)
{
    if (!s_notify_inited) return 0;

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

    /* 使用短睡眠 + 重试循环，避免 unlock -> sleep 之间丢失唤醒 */
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        kern_sleep_ms(1);
        elapsed++;

        xeros_spinlock_lock(&s_notify_lock);
        current = cur->notify_value;
        satisfied = wait_all
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
        xeros_spinlock_unlock(&s_notify_lock);
    }

    xeros_spinlock_lock(&s_notify_lock);
    cur->notify_state = KERN_NOTIFY_NOT_WAITING;
    xeros_spinlock_unlock(&s_notify_lock);
    return 0;
}
