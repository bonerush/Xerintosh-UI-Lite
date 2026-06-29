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
#include "kern_resource.h"
#include "kern_port.h"
#include "kern_kmalloc.h"
#include "kern_stats.h"
#include "kern_timer.h"

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

/* 访问器实现 */
kern_task_t *kern_task_list_head(void) { return g_task_list; }
kern_task_t *kern_task_list_tail(void) { return g_task_list_tail; }
void         kern_task_list_set_head(kern_task_t *task) { g_task_list = task; }
void         kern_task_list_set_tail(kern_task_t *task) { g_task_list_tail = task; }

kern_task_t *kern_current_task(void) { return g_current_task; }
void         kern_set_current_task(kern_task_t *task) { g_current_task = task; }

uint32_t     kern_sched_ticks(void) { return g_sched_ticks; }
void         kern_set_need_resched(bool need) { g_need_resched = need; }
bool         kern_need_resched(void) { return g_need_resched; }

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
    /* 运行时自动栈增长已禁用：
     *   kern_task_stack_grow() 会销毁任务已保存的上下文
     *   (ucontext/jmp_buf)，导致阻塞/睡眠中的任务被重启。
     *   栈大小应在 kern_spawn 时通过 stack_min 参数指定。
     *   若运行时出现高水位告警，请增加创建时的栈大小。 */
    (void)usage;  /* usage 仅用于上方的压力日志 */
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

/* ═══ 初始化公共辅助函数 ═══ */

static void sched_reset_common(void)
{
    g_task_list        = NULL;
    g_task_list_tail   = NULL;
    g_sched_ticks      = 0;
    g_task_count       = 0;
    g_need_resched     = false;
    g_next_pid         = 0;
}

static void sched_register_default_classes(void)
{
    kern_sched_class_register(&sched_class_rr);
    kern_sched_class_register(&sched_class_fifo);
}

static kern_task_t *sched_idle_create(uint8_t cpu, const char *name)
{
    kern_task_t *idle = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (idle == NULL) {
        return NULL;
    }

    idle->pid = g_next_pid++;
    idle->state = KERN_TASK_READY;
    idle->priority = 0;
    idle->base_priority = 0;
    idle->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    idle->cpu_id = cpu;
    if (name != NULL) {
        strncpy(idle->name, name, KERN_TASK_NAME_LEN);
        idle->name[KERN_TASK_NAME_LEN] = '\0';
    }
    idle->entry = idle_entry;
    idle->arg   = NULL;

    return idle;
}

void kern_sched_ensure_initialized(void)
{
    if (!g_sched_initialized) {
        kern_sched_init();
    }
}

/* ═══ 初始化 ═══ */

#ifdef NATIVE_TEST

static void sched_native_soft_reset(void)
{
    /* Native 测试环境：多次调用 kern_sched_init 时期望清理残留任务，
     * 但保留 idle 和系统守护任务以避免破坏依赖它们的测试。
     * 这里只做链表清理：把非 idle/timerd 任务从全局链表中摘除并置为 ZOMBIE，
     * 不释放内存，避免破坏可能仍被引用的 TCB。
     * 注意：保留 g_sched_ticks 单调递增，避免睡眠任务 wake_time 因计数器回绕而失效。 */
#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) { asm volatile("nop"); }
#endif
    uint32_t saved_ticks = g_sched_ticks;
    kern_task_t *t = g_task_list;
    kern_task_t *head = NULL;
    kern_task_t *tail = NULL;
    g_task_count = 0;

    while (t != NULL) {
        kern_task_t *next = t->next;
        bool is_idle = (t == g_idle_task);
        bool is_timerd = (strcmp(t->name, "timerd") == 0);
        bool keep = is_idle || is_timerd;
        if (keep) {
            t->next = NULL;
            if (is_timerd) {
                /* 软重置后 timerd 先挂起，避免在 idle 单独测试中被提前选中；
                 * 有新的定时器命令入队时由 timer 模块主动唤醒。 */
                t->state = KERN_TASK_SUSPENDED;
            }
            if (head == NULL) {
                head = tail = t;
            } else {
                tail->next = t;
                tail = t;
            }
            g_task_count++;
        } else {
            t->state = KERN_TASK_ZOMBIE;
            t->next = NULL;
        }
        t = next;
    }

    g_task_list = head;
    g_task_list_tail = tail;
    if (g_idle_task != NULL) {
        g_idle_task->state = KERN_TASK_READY;
        g_idle_task->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
    }
    sched_class_rr.task_list = g_task_list;
    sched_class_rr.task_list_tail = tail;
    sched_class_fifo.task_list = NULL;
    sched_class_fifo.task_list_tail = NULL;
    kern_timer_reset_all();
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;
    g_need_resched = false;
    g_sched_ticks = saved_ticks;
    if (g_idle_task != NULL) {
        g_per_cpu[0].idle_task = g_idle_task;
        g_per_cpu[0].current_task = g_idle_task;
        g_per_cpu[0].last_picked = g_idle_task;
        g_per_cpu[0].task_count = g_task_count;
    }
#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif
}

void kern_sched_init(void)
{
    g_task_list_tail = NULL;  /* 每次 init 都重置尾指针，支持测试环境多次调用 */
    if (g_sched_initialized) {
        sched_native_soft_reset();
        return;
    }

    sched_reset_common();
    kern_smp_init();
    sched_register_default_classes();

    g_idle_task = sched_idle_create(0, "idle");
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

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

    g_idle_task->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
    g_task_list = g_idle_task;
    sched_class_rr.task_list = g_task_list;
    sched_class_rr.task_list_tail = g_idle_task;
    g_task_count = 1;
    g_task_list_tail = g_idle_task;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;
    g_per_cpu[0].idle_task = g_idle_task;
    g_per_cpu[0].current_task = g_idle_task;
    g_per_cpu[0].last_picked = g_idle_task;
    g_per_cpu[0].task_count = 1;

    /* 初始化调度器返回上下文：macOS setcontext 要求 uc_stack 有效 */
    getcontext(&g_sched_ctx);
    g_sched_ctx.uc_stack.ss_sp = s_sched_stack;
    g_sched_ctx.uc_stack.ss_size = sizeof(s_sched_stack);
    g_sched_ctx.uc_stack.ss_flags = 0;

    g_sched_initialized = true;
    kern_log(KERN_LOG_DEBUG, "scheduler initialized (native)");
}

#elif defined(XEROS_NATIVE_SCHED)

#ifdef CONFIG_SMP_ENABLED
void kern_smp_sched_loop(void *arg);
#endif

static void esp32_native_init(void)
{
    kern_port_init();

    /* 启动原生调度器的硬件 tick（1ms）*/
    kern_port_timer_set_periodic(1000);

    /* ── 创建 per-CPU idle 任务 ── */
    for (uint8_t cpu = 0; cpu < KERN_MAX_CPUS; cpu++) {
        char name[16];
        snprintf(name, sizeof(name), "xidle%d", cpu);

        kern_task_t *idle = sched_idle_create(cpu, name);
        if (idle == NULL) {
            kern_panic("failed to allocate idle task");
            return;
        }

        task_stack_init(idle, IDLE_STACK_MIN);
        idle->native_ctx = (kern_ctx_native_t *)kern_kmalloc_for_task(
            idle, sizeof(kern_ctx_native_t));
        if (idle->native_ctx == NULL || idle->stack_base == NULL) {
            kern_panic("failed to allocate idle task context/stack");
            kern_resource_release_all(idle);
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

void kern_sched_init(void)
{
    g_task_list_tail = NULL;
    if (g_sched_initialized) return;

    sched_reset_common();
    kern_smp_init();
    sched_register_default_classes();

    esp32_native_init();

    g_sched_initialized = true;
}

#endif /* XEROS_NATIVE_SCHED */

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
    if (!g_current_task) return;
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

    kern_stats_update();
    if ((g_sched_ticks % 100) == 0) {
        kern_task_stack_overflow_check(g_current_task);
    }
    if ((g_sched_ticks % 1000) == 0) {
        kern_watchdog_check();
    }

    /* 检查是否需要重新调度（时间片到期 或 任务状态变更） */
    if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;
        kern_task_t *next = pick_next_ready();
        if (next && (next != g_current_task ||
                     (g_current_task && g_current_task->state != KERN_TASK_RUNNING))) {
            kern_task_t *prev = g_current_task;
            g_current_task = next;
            kern_mpu_apply(next);
            g_switch_to_task = next;
            if (next->state != KERN_TASK_SLEEPING) next->state = KERN_TASK_RUNNING;
            if (prev != NULL) kern_stats_task_stop(prev);
            if (next != NULL) kern_stats_task_start(next);
            /* getcontext 会被恢复两次：一次直接返回，一次从任务 yield/exit 返回。
             * 用静态标志确保 swapcontext/setcontext 只执行一次，避免无限循环。 */
            s_switch_done = false;
            getcontext(&g_sched_ctx);
            if (!s_switch_done) {
                s_switch_done = true;
                if (next != prev) {
                    swapcontext(&prev->ctx, &next->ctx);
                } else {
                    /* 选中任务与逻辑当前任务相同，但尚未真正运行（典型场景：
                     * 任务 yield 后 g_current_task 被设为 idle）。此时 prev/next
                     * 指向同一上下文，swapcontext 行为未定义，改用 setcontext。 */
                    setcontext(&next->ctx);
                }
            }
        }
    }
}

#else /* XEROS_NATIVE_SCHED */

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    if (!g_current_task) return;
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
        kern_sched_reschedule();
    }
}

/**
 * @brief 执行重新调度（不递增 tick）
 * @details 当任务主动 yield/sleep 后 g_current_task 状态不再是 RUNNING，
 *          需要立即挑选下一个就绪任务，不等下一个定时器 tick。
 *          与 kern_sched_tick() 的区别：不递增 g_sched_ticks。
 */
void kern_sched_reschedule(void)
{
    if (!g_sched_initialized) return;
    if (!g_current_task) return;

    g_need_resched = false;
    kern_task_t *next = pick_next_ready();
    if (next && next != g_current_task) {
        kern_task_t *prev = g_current_task;
        if (prev != NULL && prev->state == KERN_TASK_RUNNING) {
            prev->state = KERN_TASK_READY;
        }
        if (prev != NULL) kern_stats_task_stop(prev);
        g_current_task = next;
        next->state = KERN_TASK_RUNNING;
        if (next != NULL) kern_stats_task_start(next);
        kern_mpu_apply(next);
        kern_port_switch_to(next);
    }
}

#endif

/* ═══ idle 任务入口 ═══ */

void idle_entry(void *arg)
{
    (void)arg;
    while (1) {
        /* idle 优先级最低，直接 yield 即可。
         * 不用 kern_sleep_ms(1)：idle 不应进入 SLEEPING 状态，
         * 否则 native_idle_next_wake_ms() 会将其视为需要唤醒的任务，
         * 干扰 tickless 的下一唤醒时间计算。 */
        kern_yield();
    }
}

/* ═══ Core 0 调度器循环 ═══ */

#ifdef CONFIG_SMP_ENABLED

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

    /* per-CPU 调度循环：
     * - g_sched_ticks 仅在硬件定时器真正触发时递增（1kHz），不在每次循环迭代递增。
     *   这修正了先前以 ~200us 周期递增导致 kern_sleep_ms(N) 实际只睡 N/5 ms 的问题。
     * - 任务主动 yield/sleep 后，g_current_task->state != RUNNING，
     *   调用 kern_sched_reschedule() 立即挑选下一个任务，不等待下一个定时器 tick。
     * - kern_port_idle() 在没有就绪任务时进入低功耗等待（tickless 或 1ms 定时器同步等待）。 */
    while (1) {
        bool had_tick = kern_port_preempt_consume();
        if (had_tick) {
            g_sched_ticks++;
            g_need_resched = true;
        }

        reap_zombies();

        /* 调度类 tick 回调仅在硬件 tick 时执行（时间片递减等） */
        if (had_tick) {
            for (int i = 0; i < g_sched_class_count; i++) {
                if (g_sched_classes[i] && g_sched_classes[i]->tick) {
                    g_sched_classes[i]->tick(g_current_task);
                }
            }
        }

        sched_check_stack_pressure(g_current_task);
        sched_notify_memory_pressure();

        /* 检查是否需要重新调度（任务 yield/sleep 或时间片到期） */
        if (g_need_resched || (g_current_task && g_current_task->state != KERN_TASK_RUNNING)) {
            kern_sched_reschedule();
        }

        kern_port_idle();
    }
}

#endif /* CONFIG_SMP_ENABLED */
