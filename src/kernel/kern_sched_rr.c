/**
 * @file   kern_sched_rr.c
 * @brief  Round-Robin 调度器类实现
 * @details Round-Robin 调度器类，封装为 kern_sched_class_t 接口实现。
 *          作为默认调度类，总是第一个注册，作为所有任务的兜底调度器。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched_rr.h"
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

/* ═══ enqueue：追加到链表尾部 ═══ */

static void sched_rr_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    /* task_list 与 g_task_list 指向同一链表，直接追加 */
    kern_task_t **head = &sched_class_rr.task_list;
    kern_task_t *t = *head;
    if (t == NULL) {
        *head = task;
        task->next = NULL;
        task_list_unlock();
        return;
    }
    while (t->next != NULL) t = t->next;
    t->next = task;
    task->next = NULL;

    task_list_unlock();
}

/* ═══ dequeue：从链表中移除 ═══ */

static void sched_rr_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    kern_task_t **head = &sched_class_rr.task_list;
    kern_task_t *prev = NULL;
    kern_task_t *t = *head;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;
            } else {
                *head = t->next;
            }
            if (g_last_picked == task) g_last_picked = NULL;
            task_list_unlock();
            return;
        }
        prev = t;
        t = t->next;
    }

    task_list_unlock();
}

/* ═══ pick_next：Round-Robin 扫描（与原始 pick_next_ready 逻辑相同） ═══ */

static kern_task_t *sched_rr_pick_next(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    uint32_t now = g_sched_ticks;

    task_list_lock();

    /* 第一遍：唤醒所有到期 sleep 任务 */
    kern_task_t *t = sched_class_rr.task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* 第二遍：Round-Robin 扫描，从 per-CPU 上一轮选中任务的下一个开始 */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next
                         : sched_class_rr.task_list;

    if (start == NULL) {
        task_list_unlock();
        return NULL;  /* 任务列表为空 */
    }

    t = start;
    kern_task_t *first_candidate = NULL;
    do {
        if (t->state == KERN_TASK_READY) {
            uint8_t tid = t->cpu_id;
            /* CPU 亲和性检查：KERN_CPU_ANY 或匹配本核心 */
            if (tid == KERN_CPU_ANY || tid == cpu) {
                g_last_picked = t;
                task_list_unlock();
                return t;
            }
            if (first_candidate == NULL) {
                first_candidate = t;  /* 记录第一个就绪但亲和性不匹配的任务 */
            }
        }
        t = t->next;
        if (t == NULL) {
            t = sched_class_rr.task_list;
        }
    } while (t != start);

    task_list_unlock();

    /* 无亲和性匹配任务时，返回 KERN_CPU_ANY 任务作为兜底 */
    return first_candidate;
}

/* ═══ tick：时间片递减 + 抢占标记 ═══ */

static void sched_rr_tick(kern_task_t *current)
{
    if (current == NULL) return;

    if (current->timeslice_remaining > 0) {
        current->timeslice_remaining--;
    }
    if (current->timeslice_remaining == 0) {
        /* 时间片用尽，触发重调度 */
        current->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
        g_need_resched = true;
    }
}

/* ═══ prio_changed：优先级变更通知（RR 不使用优先级） ═══ */

static void sched_rr_prio_changed(kern_task_t *task, uint8_t old_prio)
{
    (void)task;
    (void)old_prio;
    /* RR 不关心优先级，空实现 */
}

/* ═══ 全局 RR class 实例 ═══ */

kern_sched_class_t sched_class_rr = {
    .name          = "round-robin",
    .enqueue       = sched_rr_enqueue,
    .dequeue       = sched_rr_dequeue,
    .pick_next     = sched_rr_pick_next,
    .tick          = sched_rr_tick,
    .prio_changed  = sched_rr_prio_changed,
    .task_list     = NULL,  /* 在 kern_sched_init 中设为 g_task_list */
};
