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

/* ═══ 调度器全局状态 ═══
 * 注意：g_task_list / g_task_list_tail 仅应由下方访问器读写；
 * g_current_task / g_idle_task / g_sched_ticks / g_need_resched / g_last_picked
 * 由 kern_smp.h 以宏形式提供（per-CPU 访问）。 */

extern kern_task_t   *g_task_list;        /* 任务链表头 */
extern kern_task_t   *g_task_list_tail;   /* 任务链表尾（O(1) 追加） */

kern_task_t *kern_task_list_head(void);
kern_task_t *kern_task_list_tail(void);
void         kern_task_list_set_head(kern_task_t *task);
void         kern_task_list_set_tail(kern_task_t *task);

kern_task_t *kern_current_task(void);
void         kern_set_current_task(kern_task_t *task);

uint32_t     kern_sched_ticks(void);
void         kern_set_need_resched(bool need);
bool         kern_need_resched(void);
extern kern_pid_t     g_next_pid;         /* 下一个分配的 PID */
extern uint8_t        g_task_count;       /* 任务总数 */

#ifdef CONFIG_SMP_ENABLED
extern volatile bool  g_task_list_lock;    /* 共享任务列表自旋锁 */
void kern_smp_sched_loop(void *arg);       /* 每 CPU 调度器入口 */
#endif

#ifdef NATIVE_TEST
extern ucontext_t     g_sched_ctx;        /* 调度器上下文 */
extern kern_task_t   *g_switch_to_task;   /* makecontext 参数传递 */
#elif defined(XEROS_NATIVE_SCHED)
extern kern_ctx_native_t g_sched_ctx[KERN_MAX_CPUS];  /* 调度器原生上下文（per-CPU） */
#endif

/* ═══ 调度器核心 ═══ */

kern_task_t *pick_next_ready(void);
void kern_sched_tick(void);
void kern_sched_reschedule(void);
void kern_sched_init(void);
void kern_sched_ensure_initialized(void);
void idle_entry(void *arg);

/* 访问器实现在 kern_sched.c 中提供，避免与 kern_task.h 的 extern 声明冲突 */

/* 向后兼容：保留 kern_task_current 作为 g_current_task 的访问器 */
static inline kern_task_t *kern_task_current_compat(void) { return g_current_task; }

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
