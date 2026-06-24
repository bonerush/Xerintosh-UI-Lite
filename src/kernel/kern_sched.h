/**
 * @file   kern_sched.h
 * @brief  Xeros 调度器内部头文件
 * @details 声明调度器全局状态、核心函数和跨文件内部辅助函数。
 *          仅被 kernel 内部文件包含，不对外暴露。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SCHED_H
#define KERN_SCHED_H

#include "kern_task.h"
#include "kern_sched_class.h"
#include "kern_smp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define MAX_TASKS          KERN_MAX_TASKS

/* ═══ 调度器全局状态 ═══ */

extern kern_task_t   *g_task_list;        /* 任务链表头 */
extern kern_task_t   *g_task_list_tail;   /* 任务链表尾（O(1) 追加） */
/* g_current_task, g_idle_task, g_sched_ticks, g_need_resched, g_last_picked
   由 kern_smp.h 以宏形式提供（per-CPU 访问） */
extern kern_pid_t     g_next_pid;         /* 下一个分配的 PID */
extern uint8_t        g_task_count;       /* 任务总数 */

#ifdef CONFIG_SMP_ENABLED
extern volatile bool  g_task_list_lock;    /* 共享任务列表自旋锁 */
#endif

#ifdef NATIVE_TEST
extern ucontext_t     g_sched_ctx;        /* 调度器上下文 */
extern kern_task_t   *g_switch_to_task;   /* makecontext 参数传递 */
#elif defined(XEROS_NATIVE_SCHED)
extern kern_ctx_native_t g_sched_ctx;        /* 调度器上下文 */
#endif

/* ═══ 调度器核心 ═══ */

kern_task_t *pick_next_ready(void);
void kern_sched_tick(void);
void kern_sched_init(void);
void idle_entry(void *arg);

/* ═══ 跨文件内部辅助函数 ═══ */

void task_stack_init(kern_task_t *task, size_t stack_size);
void task_write_canary(kern_task_t *task);
void reap_zombies(void);

#ifdef NATIVE_TEST
void task_entry_trampoline(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KERN_SCHED_H */
