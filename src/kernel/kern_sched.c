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

#include "kern_sched_fifo.h"

/* ═══ IDLE 任务配置 ═══ */

#define IDLE_STACK_MIN     2048

/* ═══ 全局调度状态 ═══ */

kern_task_t   *g_task_list = NULL;
/* g_current_task, g_idle_task, g_sched_ticks, g_need_resched, g_last_picked
   由 kern_smp.h 以宏形式提供（映射到 g_per_cpu[]） */
kern_pid_t     g_next_pid = 0;
uint8_t        g_task_count = 0;
static bool    g_sched_initialized = false;

#ifdef CONFIG_SMP_ENABLED
/* 共享任务列表自旋锁 */
volatile bool  g_task_list_lock = false;
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

#ifdef CONFIG_SMP_ENABLED
void kern_smp_sched_loop(void *arg);
#endif

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    kern_smp_init();

    kern_port_init();

    /* 注册调度类：RR 为默认，FIFO 为抢占优化 */
    kern_sched_class_register(&sched_class_rr);
    kern_sched_class_register(&sched_class_fifo);

    /* ── 创建 per-CPU idle 任务 ── */
    for (uint8_t cpu = 0; cpu < KERN_MAX_CPUS; cpu++) {
        char name[16];
        snprintf(name, sizeof(name), "xidle%d", cpu);

        kern_task_t *idle = (kern_task_t *)calloc(1, sizeof(kern_task_t));
        if (idle == NULL) {
            kern_panic("failed to allocate idle task");
            return;
        }

        idle->pid = g_next_pid++;
        idle->state = KERN_TASK_READY;
        idle->priority = 0;
        idle->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
        idle->cpu_id = cpu;
        strncpy(idle->name, name, KERN_TASK_NAME_LEN);
        idle->entry = idle_entry;
        idle->arg = NULL;
        idle->stack_size = IDLE_STACK_MIN;

        idle->port_thread = kern_port_thread_spawn(
            NULL, idle, name, IDLE_STACK_MIN, idle);
        if (idle->port_thread == KERN_PORT_THREAD_NULL) {
            kern_panic("failed to create idle thread");
            free(idle);
            return;
        }

        /* 链接到全局任务列表 */
        idle->next = g_task_list;
        g_task_list = idle;
        g_task_count++;

        g_per_cpu[cpu].idle_task = idle;
        g_per_cpu[cpu].last_picked = idle;
    }

    sched_class_rr.task_list = g_task_list;

    /* ── per-CPU 初始状态 ── */
    g_per_cpu[0].current_task = g_per_cpu[0].idle_task;
    g_per_cpu[0].task_count = 1;  /* idle 计入 */
    g_per_cpu[1].current_task = g_per_cpu[1].idle_task;
    g_per_cpu[1].task_count = 1;  /* idle 计入 */

    /* ── 启动 Core 0 调度器核心 ── */
    kern_smp_start_core(0, kern_smp_sched_loop);

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-freertos, 2 cores)");
}

#endif

/* ═══ Tick ═══ */

#ifdef NATIVE_TEST

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

    /* 遍历所有调度类的 tick 回调（时间片递减、抢占标记） */
    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }

    /* 定期检查栈使用率 */
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

    /* 检查是否需要重新调度（时间片到期 或 任务状态变更） */
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
    }
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

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
}

#else /* ESP32: FreeRTOS */

void kern_sched_tick(void) /* ESP32: FreeRTOS */
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

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

/* ═══ Core 0 调度器循环 ═══ */

#ifdef CONFIG_SMP_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void kern_smp_sched_loop(void *arg)
{
    (void)arg;

    /* 设置 per-CPU 状态（运行在 Core 0，KERN_THIS_CPU = xPortGetCoreID() = 0） */
    g_per_cpu[0].current_task = g_per_cpu[0].idle_task;
    g_per_cpu[0].task_count = 1;  /* idle 计入 */
    g_per_cpu[0].last_picked = g_per_cpu[0].idle_task;

    kern_log(KERN_LOG_INFO, "SMP: core 0 scheduler entering loop");

    /* Core 0 自有调度循环 */
    while (1) {
        kern_sched_tick();
        /* 短暂让出 CPU 给 FreeRTOS idle 任务喂看门狗。
         * 信号量乒乓协议中 kern_smp_sched_loop (prio+2) 和 xidle0 (prio+1)
         * 交替抢占，优先级 0 的 FreeRTOS idle 任务被完全饿死。 */
        kern_port_idle();
    }
}

#endif /* CONFIG_SMP_ENABLED */
