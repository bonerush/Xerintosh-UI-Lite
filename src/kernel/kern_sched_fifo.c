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

/* ═══ FIFO enqueue：按优先级有序插入 ═══ */

static void sched_fifo_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    kern_task_t **head = &sched_class_fifo.task_list;
    task->next = NULL;

    /* 空链表或插入到头部（最高优先级在最前） */
    if (*head == NULL || (*head)->priority <= task->priority) {
        task->next = *head;
        *head = task;
    } else {
        kern_task_t *t = *head;
        while (t->next != NULL && t->next->priority > task->priority) {
            t = t->next;
        }
        task->next = t->next;
        t->next = task;
    }

    task->scheduler_class_id = sched_class_fifo.class_id;

    /* 如果入队任务优先级高于当前运行的任务，触发抢占 */
    if (g_current_task != NULL
        && task->state == KERN_TASK_READY
        && task->priority > g_current_task->priority) {
        g_need_resched = true;
    }

    task_list_unlock();
}

/* ═══ FIFO dequeue：从链表中移除 ═══ */

static void sched_fifo_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    kern_task_t **head = &sched_class_fifo.task_list;
    kern_task_t *prev = NULL;
    kern_task_t *t = *head;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;
                if (task == sched_class_fifo.task_list_tail) {
                    sched_class_fifo.task_list_tail = prev;
                }
            } else {
                *head = t->next;
                if (task == sched_class_fifo.task_list_tail) {
                    sched_class_fifo.task_list_tail = NULL;
                }
            }
            task->scheduler_class_id = -1;
            task_list_unlock();
            return;
        }
        prev = t;
        t = t->next;
    }

    task_list_unlock();
}

/* ═══ FIFO pick_next：选择最高优先级就绪任务 ═══ */

static kern_task_t *sched_fifo_pick_next(void)
{
    task_list_lock();

    kern_task_t *t = sched_class_fifo.task_list;
    while (t != NULL) {
        /* 唤醒到期 sleep 任务 */
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= g_sched_ticks) {
            t->state = KERN_TASK_READY;
        }
        if (t->state == KERN_TASK_READY) {
            task_list_unlock();
            return t;  /* 链表按优先级排序，第一个就绪即最高优先级 */
        }
        t = t->next;
    }

    task_list_unlock();
    return NULL;
}

/* ═══ FIFO tick：检查是否有更高优先级任务就绪 ═══ */

static void sched_fifo_tick(kern_task_t *current)
{
    if (current == NULL) return;

    task_list_lock();

    /* 检查 FIFO class 中是否有比当前任务优先级更高的就绪任务 */
    kern_task_t *t = sched_class_fifo.task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_READY && t->priority > current->priority) {
            g_need_resched = true;
            task_list_unlock();
            return;
        }
        /* 链表按优先级排序，第一个不更优则后面都不会更优 */
        if (t->priority <= current->priority) {
            break;
        }
        t = t->next;
    }

    task_list_unlock();
}

/* ═══ FIFO prio_changed：优先级变更时重新排序 ═══ */

static void sched_fifo_prio_changed(kern_task_t *task, uint8_t old_prio)
{
    if (task == NULL) return;
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
