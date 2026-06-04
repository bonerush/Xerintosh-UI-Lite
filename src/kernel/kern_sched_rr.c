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

#include <stddef.h>

/* ═══ Round-Robin 内部状态 ═══ */

static uint8_t s_rr_last_prio = 0;  /* 未使用，保留给未来扩展 */

/* ═══ enqueue：追加到链表尾部 ═══ */

static void sched_rr_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    /* task_list 与 g_task_list 指向同一链表，直接追加 */
    kern_task_t **head = &sched_class_rr.task_list;
    kern_task_t *t = *head;
    if (t == NULL) {
        *head = task;
        task->next = NULL;
        return;
    }
    while (t->next != NULL) t = t->next;
    t->next = task;
    task->next = NULL;
}

/* ═══ dequeue：从链表中移除 ═══ */

static void sched_rr_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

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
            return;
        }
        prev = t;
        t = t->next;
    }
}

/* ═══ pick_next：Round-Robin 扫描（与原始 pick_next_ready 逻辑相同） ═══ */

static kern_task_t *sched_rr_pick_next(void)
{
    uint32_t now = g_sched_ticks;

    /* 第一遍：唤醒所有到期 sleep 任务 */
    kern_task_t *t = sched_class_rr.task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* 第二遍：Round-Robin 扫描，从上一轮选中任务的下一个开始 */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next
                         : sched_class_rr.task_list;

    if (start == NULL) {
        return NULL;  /* 任务列表为空 */
    }

    t = start;
    do {
        if (t->state == KERN_TASK_READY) {
            g_last_picked = t;
            return t;
        }
        t = t->next;
        if (t == NULL) {
            t = sched_class_rr.task_list;
        }
    } while (t != start);

    /* 所有任务都不可运行 */
    return NULL;
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
