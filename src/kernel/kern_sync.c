/**
 * @file   kern_sync.c
 * @brief  Xeros 内核同步原语实现
 * @details spinlock 基于 GCC __sync_lock_test_and_set，
 *          mutex 基于自旋锁保护的所有者检查。
 *          仅在 CONFIG_SMP_ENABLED 时提供真实实现；
 *          单核模式下的空操作/简单检查在头文件中以 inline/macro 提供。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sync.h"

#ifdef CONFIG_SMP_ENABLED

/* ═══ 自旋锁 ═══ */

void xeros_spinlock_init(xeros_spinlock_t *lock)
{
    lock->locked = false;
}

void xeros_spinlock_lock(xeros_spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, true)) {
        /* 自旋等待，使用 nop 降低总线争用 */
        __asm__ volatile("nop");
    }
}

void xeros_spinlock_unlock(xeros_spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

/* ═══ 互斥锁 ═══ */

kern_err_t mutex_init(mutex_t *m)
{
    xeros_spinlock_init(&m->lock);
    m->owner           = NULL;
    m->recursive_count = 0;
    m->wait_queue      = NULL;
    return KERN_OK;
}

kern_err_t mutex_lock(mutex_t *m)
{
    kern_task_t *self = g_current_task;

    xeros_spinlock_lock(&m->lock);

    if (m->owner == NULL) {
        /* 无人持有，直接获取 */
        m->owner = self;
        m->recursive_count = 1;
        xeros_spinlock_unlock(&m->lock);
    } else if (m->owner == self) {
        /* 递归获取：计数器递增 */
        m->recursive_count++;
        xeros_spinlock_unlock(&m->lock);
    } else {
        /* 已被其他任务持有：加入等待队列并自旋 */
        /* 简单实现：内核自旋锁，短临界区直接自旋等待 */
        xeros_spinlock_unlock(&m->lock);

        while (1) {
            xeros_spinlock_lock(&m->lock);
            if (m->owner == NULL) {
                m->owner = self;
                m->recursive_count = 1;
                xeros_spinlock_unlock(&m->lock);
                return KERN_OK;
            }
            xeros_spinlock_unlock(&m->lock);
            __asm__ volatile("nop");
        }
    }

    return KERN_OK;
}

kern_err_t mutex_unlock(mutex_t *m)
{
    xeros_spinlock_lock(&m->lock);

    if (m->owner != g_current_task) {
        xeros_spinlock_unlock(&m->lock);
        return KERN_EPERM;
    }

    if (m->recursive_count > 0) {
        m->recursive_count--;
    }

    if (m->recursive_count == 0) {
        m->owner = NULL;
    }

    xeros_spinlock_unlock(&m->lock);
    return KERN_OK;
}

#endif /* CONFIG_SMP_ENABLED */
