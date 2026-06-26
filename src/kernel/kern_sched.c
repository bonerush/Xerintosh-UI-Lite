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
#include "kern_kmalloc.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef NATIVE_TEST
#include <ucontext.h>
#endif

#include "kern_sched_fifo.h"

/* ═══ IDLE 任务配置 ═══ */

#define IDLE_STACK_MIN     2048

/* ═══ 全局调度状态 ═══ */

kern_task_t   *g_task_list = NULL;
kern_task_t   *g_task_list_tail = NULL;
/* g_current_task, g_idle_task, g_sched_ticks, g_need_resched, g_last_picked
   由 kern_smp.h 以宏形式提供（映射到 g_per_cpu[]） */
kern_pid_t     g_next_pid = 0;
uint8_t        g_task_count = 0;
static bool    g_sched_initialized = false;

#ifdef CONFIG_SMP_ENABLED
/* 共享任务列表自旋锁 */
volatile bool  g_task_list_lock = false;
#endif

/* ═══ 栈压力检查（后端无关）═══ */

#define STACK_PRESSURE_THRESHOLD_PCT 75
#define STACK_GROW_THRESHOLD_PCT     85
#define STACK_GROW_CONSECUTIVE       3

static void sched_check_stack_pressure(kern_task_t *task)
{
    if (task == NULL || task == g_idle_task) return;
    if ((g_sched_ticks % 500) != 0) return;

    size_t usage = kern_task_stack_usage(task);

    if (task->stack_size > 0
        && usage > task->stack_size * STACK_PRESSURE_THRESHOLD_PCT / 100) {
        kern_log(KERN_LOG_WARN,
                 "task %s stack usage %zu/%zu (>75%%)",
                 task->name, usage, task->stack_size);
    }

#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)
    /* 仅 Native 后端：连续多次超高使用率触发自动增长 */
    static uint8_t s_grow_counter[KERN_MAX_TASKS] = {0};
    if (task->pid >= 0 && task->pid < KERN_MAX_TASKS) {
        if (task->stack_size > 0
            && usage > task->stack_size * STACK_GROW_THRESHOLD_PCT / 100) {
            s_grow_counter[task->pid]++;
            if (s_grow_counter[task->pid] >= STACK_GROW_CONSECUTIVE) {
                size_t recommended = kern_task_stack_recommend(task, 0);
                if (recommended > task->stack_size) {
                    kern_log(KERN_LOG_INFO,
                             "stack_grow: task %s %zu -> %zu",
                             task->name, task->stack_size, recommended);
                    kern_task_stack_grow(task, recommended);
                }
                s_grow_counter[task->pid] = 0;
            }
        } else {
            s_grow_counter[task->pid] = 0;
        }
    }
#endif
}

/* ═══ 内存压力分发 ═══ */

static void sched_notify_memory_pressure(void)
{
    if ((g_sched_ticks % 100) != 0) return;

    kern_kmem_pressure_level_t level = kern_kmem_pressure_level();
    for (int i = 0; i < g_sched_class_count; i++) {
        kern_sched_class_t *cls = g_sched_classes[i];
        if (cls != NULL && cls->memory_pressure != NULL) {
            cls->memory_pressure(level);
        }
    }
}

#ifdef NATIVE_TEST
ucontext_t     g_sched_ctx;
kern_task_t   *g_switch_to_task = NULL;
static uint8_t s_sched_stack[8192];  /* 调度器上下文专用栈 */
static volatile bool s_switch_done = false;
#elif defined(XEROS_NATIVE_SCHED)
kern_ctx_native_t g_sched_ctx[KERN_MAX_CPUS];
#endif

/* ═══ 初始化 ═══ */

#ifdef NATIVE_TEST

void kern_sched_init(void)
{
    g_task_list_tail = NULL;  /* 每次 init 都重置尾指针，支持测试环境多次调用 */
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    g_task_list_tail = NULL;
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
    sched_class_rr.task_list_tail = g_idle_task;
    g_idle_task->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
    g_task_count = 1;
    g_task_list_tail = g_idle_task;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;

    /* 初始化调度器返回上下文：macOS setcontext 要求 uc_stack 有效 */
    getcontext(&g_sched_ctx);
    g_sched_ctx.uc_stack.ss_sp = s_sched_stack;
    g_sched_ctx.uc_stack.ss_size = sizeof(s_sched_stack);
    g_sched_ctx.uc_stack.ss_flags = 0;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (native)");
}

#elif defined(XEROS_NATIVE_SCHED)

#ifdef CONFIG_SMP_ENABLED
void kern_smp_sched_loop(void *arg);
#endif

void kern_sched_init(void)
{
    g_task_list_tail = NULL;
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    g_task_list_tail = NULL;
    kern_smp_init();

    kern_port_init();

    /* 启动原生调度器的硬件 tick（1ms）。FreeRTOS 后端依赖 FreeRTOS 调度器自带
     * 时间片，因此只在 XEROS_NATIVE_SCHED 下显式启动 GPTimer。 */
#ifdef XEROS_NATIVE_SCHED
    kern_port_timer_set_periodic(1000);
#endif

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

        task_stack_init(idle, IDLE_STACK_MIN);
        idle->native_ctx = (kern_ctx_native_t *)kern_kmalloc_for_task(
            idle, sizeof(kern_ctx_native_t));
        if (idle->native_ctx == NULL || idle->stack_base == NULL) {
            kern_panic("failed to allocate idle task context/stack");
            free(idle);
            return;
        }
        memset(idle->native_ctx, 0, sizeof(kern_ctx_native_t));
        idle->native_ctx_valid = false;
        task_write_canary(idle);

        /* 链接到全局任务链表 */
        idle->next = g_task_list;
        g_task_list = idle;
        idle->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
        g_task_count++;

        g_per_cpu[cpu].idle_task = idle;
        g_per_cpu[cpu].last_picked = idle;
    }

    sched_class_rr.task_list = g_task_list;
    /* 链表通过头插法构建，tail 应指向最后一个元素（xidle0），否则后续追加会断开链表 */
    {
        kern_task_t *tail = g_task_list;
        while (tail != NULL && tail->next != NULL) {
            tail = tail->next;
        }
        sched_class_rr.task_list_tail = tail;
        g_task_list_tail = tail;
    }

    /* ── per-CPU 初始状态 ── */
    g_per_cpu[0].current_task = g_per_cpu[0].idle_task;
    g_per_cpu[0].task_count = 1;
    g_per_cpu[1].current_task = g_per_cpu[1].idle_task;
    g_per_cpu[1].task_count = 1;

    /* ── 启动 Core 1 调度器核心（Core 0 由 app_main 直接进入 kern_smp_sched_loop） ──*/
#ifdef CONFIG_SMP_ENABLED
    kern_smp_start_core(1, kern_smp_sched_loop);
#endif

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-native, 2 cores)");
}

#else /* ═══════════════ ESP32 FreeRTOS backend ═══════════════ */

#ifdef CONFIG_SMP_ENABLED
void kern_smp_sched_loop(void *arg);
#endif

void kern_sched_init(void)
{
    g_task_list_tail = NULL;  /* 每次 init 都重置尾指针，支持测试环境多次调用 */
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    g_task_list_tail = NULL;
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
        idle->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
        g_task_count++;

        g_per_cpu[cpu].idle_task = idle;
        g_per_cpu[cpu].last_picked = idle;
    }

    sched_class_rr.task_list = g_task_list;
		sched_class_rr.task_list_tail = g_task_list;  /* SMP: idle 在队尾 */
    g_task_list_tail = g_task_list;

    /* ── per-CPU 初始状态 ── */
    g_per_cpu[0].current_task = g_per_cpu[0].idle_task;
    g_per_cpu[0].task_count = 1;  /* idle 计入 */
    g_per_cpu[1].current_task = g_per_cpu[1].idle_task;
    g_per_cpu[1].task_count = 1;  /* idle 计入 */

    /* ── 启动 Core 0 调度器核心 ── */
#ifdef CONFIG_SMP_ENABLED
    kern_smp_start_core(0, kern_smp_sched_loop);
#endif

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-freertos, 2 cores)");
}

#endif

/* ═══ Native 调度器入口（ucontext 专用栈）═══ */

#ifdef NATIVE_TEST

static void sched_entry(void)
{
    while (g_sched_initialized) {
        kern_sched_tick();
    }
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

    sched_check_stack_pressure(g_current_task);
    sched_notify_memory_pressure();

    /* 检查是否需要重新调度（时间片到期 或 任务状态变更） */
    if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;
        kern_task_t *next = pick_next_ready();
        if (next && next != g_current_task) {
            kern_task_t *prev = g_current_task;
            g_current_task = next;
            kern_mpu_apply(next);
            g_switch_to_task = next;
            if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
            /* getcontext 会被恢复两次：一次直接返回，一次从任务 yield/exit 返回。
             * 用静态标志确保 swapcontext 只执行一次，避免无限循环。 */
            s_switch_done = false;
            getcontext(&g_sched_ctx);
            if (!s_switch_done) {
                s_switch_done = true;
                swapcontext(&prev->ctx, &next->ctx);
            }
        }
    }
}

#else /* ESP32: FreeRTOS / XEROS_NATIVE_SCHED fallback */

void kern_sched_tick(void) /* ESP32: FreeRTOS / XEROS_NATIVE_SCHED fallback */
{
    if (!g_sched_initialized) return;
    g_sched_ticks++;
    reap_zombies();

    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }

    sched_check_stack_pressure(g_current_task);
    sched_notify_memory_pressure();

    if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;
        kern_task_t *next = pick_next_ready();
        if (next && next != g_current_task) {
            kern_task_t *prev = g_current_task;
            if (prev != NULL && prev->state == KERN_TASK_RUNNING) {
                prev->state = KERN_TASK_READY;
            }
            g_current_task = next;
            next->state = KERN_TASK_RUNNING;
            kern_mpu_apply(next);
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
        /* idle 优先级最低，睡眠 1ms 即可，避免无限 kern_yield 造成无意义切换。 */
        kern_sleep_ms(1);
    }
}

/* ═══ Core 0 调度器循环 ═══ */

#ifdef CONFIG_SMP_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void kern_smp_sched_loop(void *arg)
{
    (void)arg;

    uint8_t cpu = kern_cpu_id();
    if (cpu >= KERN_MAX_CPUS) {
        kern_log(KERN_LOG_ERROR, "SMP: sched_loop on invalid core %u", (unsigned)cpu);
        return;
    }

    /* 设置 per-CPU 状态 */
    g_per_cpu[cpu].current_task = g_per_cpu[cpu].idle_task;
    g_per_cpu[cpu].task_count = 1;  /* idle 计入 */
    g_per_cpu[cpu].last_picked = g_per_cpu[cpu].idle_task;

    /* 通知其他核心本核心已就绪 */
    __sync_or_and_fetch(&g_cpu_ready, (uint8_t)(1u << cpu));

    kern_log(KERN_LOG_INFO, "SMP: core %u scheduler entering loop", (unsigned)cpu);

    /* per-CPU 调度循环 */
    while (1) {
        if (kern_port_preempt_consume()) {
            g_need_resched = true;
        }
        kern_sched_tick();
        kern_port_idle();
    }
}

#endif /* CONFIG_SMP_ENABLED */
