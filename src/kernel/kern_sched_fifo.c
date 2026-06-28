/**
 * @file   kern_sched_fifo.c
 * @brief  优先级 FIFO 调度器类实现
 * @details 任务按优先级排序（0 最低，255 最高），
 *          pick_next() 始终选择最高优先级就绪任务。
 *          当更高优先级任务入队时触发抢占。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched_fifo.h"
#include "kern_sched.h"
#include "kern_task.h"
#include "kern_smp.h"

#include <stddef.h>

/* ═══ SMP 自旋锁辅助 ═══ */

#ifdef CONFIG_SMP_ENABLED
static inline void task_list_lock(void) {
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
}
static inline void task_list_unlock(void) {
    __sync_lock_release(&g_task_list_lock);
}
#else
#define task_list_lock()    do {} while (0)
#define task_list_unlock()  do {} while (0)
#endif

/* ═══ 优先级桶数组（0 最低，255 最高）═══ */

static kern_task_t *g_fifo_buckets[256];
static kern_task_t *g_fifo_bucket_tails[256];

/* ═══ FIFO enqueue：按优先级桶 O(1) 追加 ═══ */

static void sched_fifo_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    uint8_t p = task->priority;
    task->next = NULL;
    task->scheduler_class_id = sched_class_fifo.class_id;

    if (g_fifo_buckets[p] == NULL) {
        g_fifo_buckets[p] = task;
    } else {
        g_fifo_bucket_tails[p]->next = task;
    }
    g_fifo_bucket_tails[p] = task;

    /* 如果入队任务优先级高于当前运行的任务，触发抢占 */
    kern_task_t *current = kern_current_task();
    if (current != NULL
        && task->state == KERN_TASK_READY
        && task->priority > current->priority) {
        kern_set_need_resched(true);
    }

    task_list_unlock();
}

/* ═══ FIFO dequeue：从链表中移除 ═══ */

static void sched_fifo_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    for (int p = 255; p >= 0; p--) {
        kern_task_t **pp = &g_fifo_buckets[p];
        kern_task_t *prev = NULL;
        while (*pp != NULL) {
            if (*pp == task) {
                *pp = task->next;
                if (g_fifo_bucket_tails[p] == task) {
                    g_fifo_bucket_tails[p] = prev;
                }
                task->scheduler_class_id = -1;
                task_list_unlock();
                return;
            }
            prev = *pp;
            pp = &(*pp)->next;
        }
    }

    task->scheduler_class_id = -1;
    task_list_unlock();
}

/* ═══ FIFO pick_next：选择最高优先级就绪任务 ═══ */

static kern_task_t *sched_fifo_pick_next(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    uint32_t now = kern_sched_ticks();

    task_list_lock();

    for (int p = 255; p >= 0; p--) {
        kern_task_t *t = g_fifo_buckets[p];
        while (t != NULL) {
            /* 唤醒到期 sleep 任务 */
            if (t->state == KERN_TASK_SLEEPING && (int32_t)(now - t->wake_time) >= 0) {
                t->state = KERN_TASK_READY;
            }
            if (t->state == KERN_TASK_SUSPENDED) {
                t = t->next;
                continue;
            }
            if (t->state == KERN_TASK_READY) {
                uint8_t tid = t->cpu_id;
                /* CPU 亲和性检查：KERN_CPU_ANY 或匹配本核心 */
                if (tid == KERN_CPU_ANY || tid == cpu) {
                    t->state = KERN_TASK_RUNNING;
                    task_list_unlock();
                    return t;
                }
            }
            t = t->next;
        }
    }

    task_list_unlock();
    return NULL;
}

/* ═══ FIFO tick：检查是否有更高优先级任务就绪 ═══ */

static void sched_fifo_tick(kern_task_t *current)
{
    if (current == NULL) return;

    task_list_lock();

    for (int p = 255; p > current->priority; p--) {
        kern_task_t *t = g_fifo_buckets[p];
        while (t != NULL) {
            if (t->state == KERN_TASK_READY) {
                kern_set_need_resched(true);
                task_list_unlock();
                return;
            }
            t = t->next;
        }
    }

    task_list_unlock();
}

/* ═══ FIFO prio_changed：优先级变更时重新排序 ═══ */

static void sched_fifo_prio_changed(kern_task_t *task, uint8_t old_prio)
{
    if (task == NULL) return;
    if (task->scheduler_class_id != sched_class_fifo.class_id) return;
    /* 移除再按新优先级重新插入 */
    sched_fifo_dequeue(task);
    sched_fifo_enqueue(task);
    (void)old_prio;
}

/* ═══ 全局 FIFO class 实例 ═══ */

kern_sched_class_t sched_class_fifo = {
    .name             = "priority-fifo",
    .enqueue          = sched_fifo_enqueue,
    .dequeue          = sched_fifo_dequeue,
    .pick_next        = sched_fifo_pick_next,
    .tick             = sched_fifo_tick,
    .prio_changed     = sched_fifo_prio_changed,
    .memory_pressure  = NULL,
    .task_list        = NULL,
    .task_list_tail   = NULL,
};
