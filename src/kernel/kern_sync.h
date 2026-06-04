/**
 * @file   kern_sync.h
 * @brief  Xeros 内核同步原语头文件
 * @details 定义 spinlock_t 和 mutex_t 数据结构及操作接口。
 *          当 CONFIG_SMP_ENABLED 未定义时，spinlock 退化为空操作，
 *          mutex 退化为简单的所有者检查（单核无竞争）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SYNC_H
#define KERN_SYNC_H

#include "kern_types.h"
#include "kern_smp.h"
#include "kern_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 自旋锁 ═══ */

/**
 * @brief 自旋锁
 * @note  SMP 模式：使用 __sync_lock_test_and_set 原子操作
 *        单核模式：退化为空操作
 */
typedef struct {
    volatile bool locked;
} spinlock_t;

#ifdef CONFIG_SMP_ENABLED

void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);

#else

#define spinlock_init(l)     do { (l)->locked = false; } while (0)
#define spinlock_lock(l)     do {} while (0)
#define spinlock_unlock(l)   do {} while (0)

#endif

/* ═══ 互斥锁 ═══ */

/**
 * @brief 互斥锁
 * @note  SMP 模式：自旋锁保护的所有者检查 + 等待队列
 *        单核模式：简单的所有者标记
 */
typedef struct {
    spinlock_t    lock;        /* 内部自旋锁 */
    kern_task_t  *owner;       /* 当前持有者 */
    kern_task_t  *wait_queue;  /* 等待队列头 */
} mutex_t;

#ifdef CONFIG_SMP_ENABLED

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#else

static inline void mutex_init(mutex_t *m)
{
    spinlock_init(&m->lock);
    m->owner      = NULL;
    m->wait_queue = NULL;
}

static inline void mutex_lock(mutex_t *m)
{
    /* 单核无竞争，直接标记所有权 */
    m->owner = g_current_task;
}

static inline void mutex_unlock(mutex_t *m)
{
    m->owner = NULL;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* KERN_SYNC_H */
