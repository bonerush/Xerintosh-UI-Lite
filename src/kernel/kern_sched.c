/**
 * @file   kern_sched.c
 * @brief  Xeros 调度器核心实现
 * @details 包含全局调度状态、调度初始化、tick 分发、
 *          Round-Robin 就绪任务选择、idle 任务入口。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched.h"
#include "kern_task.h"
#include "kern_init.h"
#include "kern_port.h"

#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST
#include <ucontext.h>
#elif defined(XEROS_NATIVE_SCHED)
#include "kern_ctx_esp32.h"
#include <setjmp.h>
#include <esp_timer.h>
#endif

/* ═══ IDLE 任务配置 ═══ */

#define IDLE_STACK_MIN     2048   /* idle FreeRTOS 任务栈（需容纳 printf 调用） */

/* ═══ 全局调度状态 ═══ */

kern_task_t   *g_task_list = NULL;        /* 任务链表头 */
kern_task_t   *g_current_task = NULL;     /* 当前运行的任务 */
kern_task_t   *g_idle_task = NULL;        /* idle 任务（兜底） */
kern_task_t   *g_last_picked = NULL;      /* 上一轮选中的任务（Round-Robin 扫描起点） */
kern_pid_t     g_next_pid = 0;            /* 下一个分配的 PID */
uint8_t        g_task_count = 0;          /* 任务总数 */
uint32_t       g_sched_ticks = 0;         /* 调度 tick 计数 */
static bool    g_sched_initialized = false;

#ifdef NATIVE_TEST
ucontext_t     g_sched_ctx;               /* 调度器上下文 */
kern_task_t   *g_switch_to_task = NULL;   /* makecontext 参数传递 */
#elif defined(XEROS_NATIVE_SCHED)
kern_ctx_t     g_sched_ctx;               /* 调度器上下文 */
#endif

/* ═══ 初始化 ═══ */

#ifdef NATIVE_TEST

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
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
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;  /* 首轮从 idle 之后开始扫描 */

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (native)");
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    /* 创建 idle 任务（使用手动栈 + setjmp/longjmp） */
    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    task_stack_init(g_idle_task, IDLE_STACK_MIN);

    uint8_t *stack_top = g_idle_task->stack_base + g_idle_task->stack_size;
    kern_ctx_init(&g_idle_task->ctx, g_idle_task->stack_base, stack_top,
                  idle_entry, NULL);
    task_write_canary(g_idle_task);

    g_task_list = g_idle_task;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-native-sched)");
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器 + 协作锁) ═══════════════ */

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    /* 初始化可移植层（创建调度基础设施） */
    kern_port_init();

    /* 创建 idle 任务 */
    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    g_idle_task->stack_size = IDLE_STACK_MIN;

    /* 通过可移植层创建底层线程 */
    g_idle_task->port_thread = kern_port_thread_spawn(
        NULL,                    /* entry 为 NULL：使用 task_wrapper（由 port 层管理） */
        g_idle_task,             /* arg = TCB */
        "xidle",
        IDLE_STACK_MIN,
        g_idle_task
    );
    if (g_idle_task->port_thread == KERN_PORT_THREAD_NULL) {
        kern_panic("failed to create idle thread");
        free(g_idle_task);
        return;
    }

    g_task_list = g_idle_task;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;  /* 首轮从 idle 之后开始扫描 */

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-freertos-wrapper)");
}

#endif

/* ═══ Tick ═══ */

#ifdef NATIVE_TEST

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    reap_zombies();  /* 回收 ZOMBIE 任务 TCB */

    /* 栈使用率监控：超过 75% 时警告 */
    if (g_current_task != NULL && g_current_task != g_idle_task
        && (g_sched_ticks % 500) == 0) {  /* 每 500 tick 检查一次 */
        size_t usage = kern_task_stack_usage(g_current_task);
        if (usage > 0 && g_current_task->stack_size > 0
            && usage > g_current_task->stack_size * 3 / 4) {
            kern_log(KERN_LOG_WARN,
                     "task %s stack usage %zu/%zu (>75%%)",
                     g_current_task->name,
                     usage,
                     g_current_task->stack_size);
        }
    }

    kern_task_t *volatile next = pick_next_ready();
    if (next == NULL) return;

    g_current_task = next;
    g_switch_to_task = next;
    swapcontext(&g_sched_ctx, &next->ctx);
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    reap_zombies();  /* 回收 ZOMBIE 任务 TCB */

    kern_task_t *next = pick_next_ready();

    if (next == NULL) {
        if (g_sched_ticks % 1000 == 0) {
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        }
        kern_port_idle();
        return;
    }

    g_current_task = next;
    if (next->state != KERN_TASK_SLEEPING) {
        next->state = KERN_TASK_RUNNING;
    }

    /* setjmp 保存调度器上下文，longjmp 切换到目标任务 */
    if (setjmp(g_sched_ctx.jmp) == 0) {
        longjmp(next->ctx.jmp, 1);  /* 跳转到任务的上次 setjmp 点 */
    }
    /* 任务 yield/exit 后回到此处 */
}

#else /* ESP32: 协作式 tick，通过可移植层释放令牌给下一个任务 */

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    reap_zombies();  /* 回收 ZOMBIE 任务 TCB */

    kern_task_t *next = pick_next_ready();
    
    if (next == NULL) {
        /* 无就绪任务，通过可移植层短暂让出 CPU */
        if (g_sched_ticks % 1000 == 0) {
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        }
        kern_port_idle();
        return;
    }

    g_current_task = next;
    if (next->state != KERN_TASK_SLEEPING) {
        next->state = KERN_TASK_RUNNING;
    }

    /* 通过可移植层切换到目标任务（阻塞等待任务 yield/exit） */
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

/* ═══ Round-Robin 就绪任务选择 ═══ */

#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)

kern_task_t *pick_next_ready(void)
{
    uint32_t now = g_sched_ticks;

    /* 第一遍：唤醒所有到期 sleep 任务 */
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* 第二遍：Round-Robin 扫描，从上一轮选中任务的下一个开始 */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next
                         : g_task_list;

    t = start;
    do {
        if (t->state == KERN_TASK_READY) {
            g_last_picked = t;
            return t;
        }
        t = t->next;
        if (t == NULL) {
            t = g_task_list;
        }
    } while (t != start);

    /* 所有任务都不可运行：返回 NULL */
    return NULL;
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器) ═══════════════ */

kern_task_t *pick_next_ready(void)
{
    uint32_t now = g_sched_ticks;

    /* 第一遍：唤醒所有到期 sleep 任务 */
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* 第二遍：Round-Robin 扫描 */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next
                         : g_task_list;

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
            t = g_task_list;
        }
    } while (t != start);

    /* 所有任务都不可运行 */
    return NULL;
}

#endif
