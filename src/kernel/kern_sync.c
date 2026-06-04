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

void spinlock_init(spinlock_t *lock)
{
    lock->locked = false;
}

void spinlock_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, true)) {
        /* 自旋等待，使用 nop 降低总线争用 */
        __asm__ volatile("nop");
    }
}

void spinlock_unlock(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

/* ═══ 互斥锁 ═══ */

void mutex_init(mutex_t *m)
{
    spinlock_init(&m->lock);
    m->owner      = NULL;
    m->wait_queue = NULL;
}

void mutex_lock(mutex_t *m)
{
    kern_task_t *self = g_current_task;

    spinlock_lock(&m->lock);

    if (m->owner == NULL) {
        /* 无人持有，直接获取 */
        m->owner = self;
        spinlock_unlock(&m->lock);
    } else if (m->owner == self) {
        /* 递归获取：允许但记录警告 */
        spinlock_unlock(&m->lock);
    } else {
        /* 已被其他任务持有：加入等待队列并自旋 */
        /* 简单实现：当前为协作式调度，直接自旋等待 */
        spinlock_unlock(&m->lock);

        while (1) {
            spinlock_lock(&m->lock);
            if (m->owner == NULL) {
                m->owner = self;
                spinlock_unlock(&m->lock);
                return;
            }
            spinlock_unlock(&m->lock);
            __asm__ volatile("nop");
        }
    }
}

void mutex_unlock(mutex_t *m)
{
    spinlock_lock(&m->lock);
    m->owner = NULL;
    spinlock_unlock(&m->lock);
}

#endif /* CONFIG_SMP_ENABLED */
