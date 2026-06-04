/**
 * @file   kern_sched.c
 * @brief  Xeros 调度器核心实现
 * @details 包含全局调度状态、调度初始化、tick 分发、
 *          可插拔调度类注册与选择、idle 任务入口。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched.h"
#include "kern_sched_class.h"
#include "kern_sched_rr.h"
#include "kern_task.h"
#include "kern_mpu.h"
#include "kern_init.h"
#include "kern_port.h"

#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST
#include <ucontext.h>
#elif defined(XEROS_NATIVE_SCHED)
#include "kern_ctx_esp32.h"
#include <setjmp.h>
#endif

#ifdef CONFIG_PREEMPT_ENABLED
#include "kern_sched_fifo.h"
#endif

/* ═══ IDLE 任务配置 ═══ */

#define IDLE_STACK_MIN     2048

/* ═══ 全局调度状态 ═══ */

kern_task_t   *g_task_list = NULL;
/* g_current_task, g_idle_task, g_sched_ticks, g_need_resched
   由 kern_smp.h 以宏形式提供（映射到 g_per_cpu[]） */
kern_task_t   *g_last_picked = NULL;
kern_pid_t     g_next_pid = 0;
uint8_t        g_task_count = 0;
static bool    g_sched_initialized = false;

#ifdef CONFIG_PREEMPT_ENABLED
/* g_need_resched 由 kern_smp.h 宏映射到 g_per_cpu[].need_resched */
#endif

#ifdef NATIVE_TEST
ucontext_t     g_sched_ctx;
kern_task_t   *g_switch_to_task = NULL;
#elif defined(XEROS_NATIVE_SCHED)
kern_ctx_t     g_sched_ctx;
#endif

/* ═══ 初始化 ═══ */

#ifdef NATIVE_TEST

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    kern_smp_init();

    /* 注册默认调度类 */
    kern_sched_class_register(&sched_class_rr);

    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    g_idle_task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    task_stack_init(g_idle_task, IDLE_STACK_MIN);

    if (getcontext(&g_idle_task->ctx) < 0) {
        kern_panic("getcontext for idle task failed");
        return;
    }
    g_idle_task->ctx.uc_stack.ss_sp    = g_idle_task->stack_base;
    g_idle_task->ctx.uc_stack.ss_size  = g_idle_task->stack_size;
    g_idle_task->ctx.uc_stack.ss_flags = 0;
    g_idle_task->ctx.uc_link = NULL;

    g_switch_to_task = g_idle_task;
    makecontext(&g_idle_task->ctx, task_entry_trampoline, 0);
    task_write_canary(g_idle_task);

    g_task_list = g_idle_task;
    sched_class_rr.task_list = g_task_list;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (native)");
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_init(void) /* XEROS_NATIVE_SCHED */
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    kern_smp_init();

    kern_sched_class_register(&sched_class_rr);

    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    g_idle_task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    task_stack_init(g_idle_task, IDLE_STACK_MIN);

    uint8_t *stack_top = g_idle_task->stack_base + g_idle_task->stack_size;
    kern_ctx_init(&g_idle_task->ctx, g_idle_task->stack_base, stack_top,
                  idle_entry, NULL);
    task_write_canary(g_idle_task);

    g_task_list = g_idle_task;
    sched_class_rr.task_list = g_task_list;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-native-sched)");
}

#else /* ═══════════════ ESP32 (FreeRTOS) ═══════════════ */

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    kern_smp_init();

    kern_port_init();

    /* 注册调度类：RR 为默认，FIFO 为抢占优化 */
    kern_sched_class_register(&sched_class_rr);
#ifdef CONFIG_PREEMPT_ENABLED
    kern_sched_class_register(&sched_class_fifo);
#endif

    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    g_idle_task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    g_idle_task->stack_size = IDLE_STACK_MIN;

    g_idle_task->port_thread = kern_port_thread_spawn(
        NULL, g_idle_task, "xidle", IDLE_STACK_MIN, g_idle_task);
    if (g_idle_task->port_thread == KERN_PORT_THREAD_NULL) {
        kern_panic("failed to create idle thread");
        free(g_idle_task);
        return;
    }

    g_task_list = g_idle_task;
    sched_class_rr.task_list = g_task_list;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-freertos)");
}

#endif

/* ═══ Tick ═══ */

#ifdef NATIVE_TEST

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

#ifdef CONFIG_PREEMPT_ENABLED
    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }
    if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;
        kern_task_t *next = pick_next_ready();
        if (next && next != g_current_task) {
            g_current_task = next;
            kern_mpu_apply(next);
            g_switch_to_task = next;
            if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
            swapcontext(&g_sched_ctx, &next->ctx);
        }
        return;
    }
#endif

    if (g_current_task != NULL && g_current_task != g_idle_task
        && (g_sched_ticks % 500) == 0) {
        size_t usage = kern_task_stack_usage(g_current_task);
        if (usage > 0 && g_current_task->stack_size > 0
            && usage > g_current_task->stack_size * 3 / 4) {
            kern_log(KERN_LOG_WARN,
                     "task %s stack usage %zu/%zu (>75%%)",
                     g_current_task->name, usage, g_current_task->stack_size);
        }
    }

    kern_task_t *volatile next = pick_next_ready();
    if (next == NULL) return;
    g_current_task = next;
    kern_mpu_apply(next);
    g_switch_to_task = next;
    swapcontext(&g_sched_ctx, &next->ctx);
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

#ifdef CONFIG_PREEMPT_ENABLED
    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }
    if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;
        kern_task_t *next = pick_next_ready();
        if (next) {
            g_current_task = next;
            kern_mpu_apply(next);
            if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
            if (setjmp(g_sched_ctx.jmp) == 0) longjmp(next->ctx.jmp, 1);
        }
        return;
    }
#endif

    kern_task_t *next = pick_next_ready();
    if (next == NULL) {
        if (g_sched_ticks % 1000 == 0)
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        kern_port_idle();
        return;
    }
    g_current_task = next;
    kern_mpu_apply(next);
    if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
    if (setjmp(g_sched_ctx.jmp) == 0) longjmp(next->ctx.jmp, 1);
}

#else /* ESP32: FreeRTOS */

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

#ifdef CONFIG_PREEMPT_ENABLED
    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }
    if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;
        kern_task_t *next = pick_next_ready();
        if (next) {
            g_current_task = next;
            kern_mpu_apply(next);
            if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
            kern_port_switch_to(next);
        }
        return;
    }
#endif

    kern_task_t *next = pick_next_ready();
    if (next == NULL) {
        if (g_sched_ticks % 1000 == 0)
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        kern_port_idle();
        return;
    }
    g_current_task = next;
    kern_mpu_apply(next);
    if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
    kern_port_switch_to(next);
}

#endif

/* ═══ idle 任务入口 ═══ */

void idle_entry(void *arg)
{
    (void)arg;
    while (1) {
        kern_yield();
    }
}
