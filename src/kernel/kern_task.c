/**
 * @file   kern_task.c
 * @brief  Xeros 协作式多任务调度器实现
 * @details 实现 Round-Robin 协作式调度器。
 *
 *          Native 测试：使用 POSIX ucontext（每任务独立栈 + swapcontext）
 *          ESP32：使用 setjmp/longjmp（FreeRTOS 任务天然隔离栈）
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_task.h"
#include "kern_init.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ═══ 内部常量 ═══ */

#define MAX_TASKS          KERN_MAX_TASKS
#define IDLE_STACK_SIZE    512

/* ═══ 全局调度状态 ═══ */

static kern_task_t   *g_task_list = NULL;      /* 任务链表头 */
static kern_task_t   *g_current_task = NULL;   /* 当前运行的任务 */
static kern_task_t   *g_idle_task = NULL;      /* idle 任务（兜底） */
static kern_pid_t     g_next_pid = 0;          /* 下一个分配的 PID */
static uint8_t        g_task_count = 0;        /* 任务总数 */
static uint32_t       g_sched_ticks = 0;       /* 调度 tick 计数 */
static bool           g_sched_initialized = false;

/*
 * 调度器上下文
 *
 * Native: ucontext_t，通过 swapcontext 与任务互相切换
 * ESP32:  jmp_buf，  通过 setjmp/longjmp 进行锚点跳转
 */
static kern_ctx_t     g_sched_ctx;

/*
 * makecontext 参数传递：uc_mcontext 只能传 int 参数，
 * 64 位指针通过此全局变量传递。
 */
#ifdef NATIVE_TEST
static kern_task_t   *g_switch_to_task = NULL;
#endif

/* ═══ 前向声明 ═══ */

static void idle_entry(void *arg);
static kern_task_t *pick_next_ready(void);
static void task_stack_init(kern_task_t *task, size_t stack_size);
static void task_write_canary(kern_task_t *task);

#ifdef NATIVE_TEST
static void task_entry_trampoline(void);
#endif

/* ═══ 初始化 ═══ */

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

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
    task_stack_init(g_idle_task, IDLE_STACK_SIZE);

    /*
     * Native: makecontext 为 idle 设置执行栈和入口
     * ESP32:  setjmp 保存 idle 上下文
     */
#ifdef NATIVE_TEST
    if (getcontext(&g_idle_task->ctx) < 0) {
        kern_panic("getcontext for idle task failed");
        return;
    }
    g_idle_task->ctx.uc_stack.ss_sp    = g_idle_task->stack_base;
    g_idle_task->ctx.uc_stack.ss_size  = g_idle_task->stack_size;
    g_idle_task->ctx.uc_stack.ss_flags = 0;
    g_idle_task->ctx.uc_link = NULL;  /* 永不返回 */

    g_switch_to_task = g_idle_task;
    makecontext(&g_idle_task->ctx, task_entry_trampoline, 0);

    /* makecontext 会写入栈顶设置数据，canary 需在之后重写 */
    task_write_canary(g_idle_task);
#else
    if (setjmp(g_idle_task->ctx) == 0) {
        /* 初始化完成 */
    }
#endif

    g_task_list = g_idle_task;
    g_task_count = 1;
    g_current_task = g_idle_task;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized");
}

/* ═══ Tick ═══ */

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    /* 查找下一个就绪任务（跳过当前任务和僵尸任务） */
    kern_task_t *volatile next = pick_next_ready();
    if (next == NULL) {
        return;  /* 没有其他就绪任务，继续运行当前 */
    }

    g_current_task = next;

#ifdef NATIVE_TEST
    /*
     * Native: swapcontext 保存调度器上下文，切换到任务上下文。
     * 任务通过 swapcontext(&task->ctx, &g_sched_ctx) 返回。
     */
    g_switch_to_task = next;
    swapcontext(&g_sched_ctx, &next->ctx);
#else
    /*
     * ESP32: setjmp/longjmp 锚点跳转。
     */
    if (setjmp(g_sched_ctx) == 0) {
        if (!next->has_run) {
            next->has_run = true;
            next->state = KERN_TASK_RUNNING;
            if (setjmp(next->ctx) == 0) {
                next->entry(next->arg);
                next->state = KERN_TASK_ZOMBIE;
                longjmp(g_sched_ctx, 1);
            }
        } else {
            next->state = KERN_TASK_RUNNING;
            longjmp(next->ctx, 1);
        }
    }
#endif
    /* 从任务返回到调度器 */
}

/* ═══ 任务创建 ═══ */

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    /* 确保调度器已初始化 */
    if (!g_sched_initialized) {
        kern_sched_init();
    }

    /* 分配 TCB */
    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->entry = entry;
    task->arg = arg;

    /* 设置名称 */
    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    /* 分配独立执行栈 */
    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_STACK_MIN;
    task_stack_init(task, stack_sz);

    /*
     * Native: 用 makecontext 设置入口和独立栈
     * ESP32:  仅标记为未运行，首次 tick 时用 setjmp 初始化
     */
#ifdef NATIVE_TEST
    if (getcontext(&task->ctx) < 0) {
        kern_log(KERN_LOG_WARN, "getcontext failed for task %s", task->name);
        free(task->stack_base);
        free(task);
        return KERN_ERR;
    }
    task->ctx.uc_stack.ss_sp    = task->stack_base;
    task->ctx.uc_stack.ss_size  = task->stack_size;
    task->ctx.uc_stack.ss_flags = 0;
    task->ctx.uc_link = NULL;

    g_switch_to_task = task;
    makecontext(&task->ctx, task_entry_trampoline, 0);
    task_write_canary(task);
#else
    task->has_run = false;
#endif

    /* 挂载到任务链表尾部 */
    if (g_task_list == NULL) {
        g_task_list = task;
    } else {
        kern_task_t *t = g_task_list;
        while (t->next != NULL) t = t->next;
        t->next = task;
    }
    g_task_count++;

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

/* ═══ Yield ═══ */

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_READY;
    g_current_task = g_idle_task;

#ifdef NATIVE_TEST
    swapcontext(&cur->ctx, &g_sched_ctx);
#else
    if (setjmp(cur->ctx) == 0) {
        longjmp(g_sched_ctx, 1);
    }
#endif
}

/* ═══ Exit ═══ */

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;

#ifdef NATIVE_TEST
    /* setcontext 永不返回 */
    setcontext(&g_sched_ctx);
#else
    longjmp(g_sched_ctx, 1);
#endif
}

/* ═══ Sleep ═══ */

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;
    g_current_task = g_idle_task;

#ifdef NATIVE_TEST
    swapcontext(&cur->ctx, &g_sched_ctx);
#else
    if (setjmp(cur->ctx) == 0) {
        longjmp(g_sched_ctx, 1);
    }
#endif
}

/* ═══ 查询接口 ═══ */

kern_task_t *kern_task_current(void)
{
    return g_current_task;
}

uint8_t kern_task_count(void)
{
    return g_task_count;
}

kern_task_t *kern_task_get(kern_pid_t pid)
{
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->pid == pid) return t;
        t = t->next;
    }
    return NULL;
}

kern_task_t *kern_task_list_head(void)
{
    return g_task_list;
}

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;

    /* 从栈底向上扫描，找到第一个非 0xAA 字节来估算使用量 */
    size_t used = 0;
    for (size_t i = 0; i < task->stack_size; i++) {
        if (task->stack_base[i] != 0xAA) {
            used = task->stack_size - i;
            break;
        }
    }

    /* 检查金丝雀 */
    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary;
        memcpy(&canary, task->stack_base, sizeof(uint32_t));
        if (canary != KERN_STACK_CANARY) {
            kern_log(KERN_LOG_WARN, "stack canary corrupted for task %s (pid=%d)",
                     task->name, task->pid);
        }
    }

    return (used > 0) ? used : 1;
}

uint32_t kern_task_stack_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;
    if (task->stack_size < sizeof(uint32_t)) return 0;

    uint32_t canary;
    memcpy(&canary, task->stack_base, sizeof(uint32_t));
    return canary;
}

/* ═══ 内部函数 ═══ */

static void idle_entry(void *arg)
{
    (void)arg;
    while (1) {
        kern_yield();
    }
}

#ifdef NATIVE_TEST
/*
 * task_entry_trampoline:
 * makecontext 的目标函数。通过全局变量 g_switch_to_task 获取
 * 目标任务指针，调用其入口函数。
 *
 * 入口函数返回后，将任务标记为 ZOMBIE 并切回调度器。
 */
static void task_entry_trampoline(void)
{
    kern_task_t *task = g_switch_to_task;

    if (task == NULL || task->entry == NULL) {
        setcontext(&g_sched_ctx);
        return;
    }

    task->entry(task->arg);

    /* 入口函数返回 → 任务结束 */
    task->state = KERN_TASK_ZOMBIE;
    g_current_task = g_idle_task;
    setcontext(&g_sched_ctx);
}
#endif

static kern_task_t *pick_next_ready(void)
{
    kern_task_t *t = g_task_list;
    uint32_t now = g_sched_ticks;

    while (t != NULL) {
        /* 唤醒到期的休眠任务 */
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }

        /* 选第一个不同于当前任务的 READY 任务 */
        if (t->state == KERN_TASK_READY && t != g_current_task) {
            return t;
        }
        t = t->next;
    }

    /* 如果只有 idle 且它刚 yield 了（是当前任务且 READY） */
    if (g_current_task != g_idle_task || g_current_task->state != KERN_TASK_READY) {
        t = g_task_list;
        while (t != NULL) {
            if (t->state == KERN_TASK_READY && t != g_idle_task) {
                return t;
            }
            t = t->next;
        }
    }

    return NULL;
}

static void task_stack_init(kern_task_t *task, size_t stack_size)
{
    if (stack_size > KERN_STACK_MAX) stack_size = KERN_STACK_MAX;
    if (stack_size < KERN_STACK_MIN) stack_size = KERN_STACK_MIN;

    task->stack_size = stack_size;
    task->stack_base = (uint8_t *)malloc(stack_size);
    if (task->stack_base == NULL) {
        kern_log(KERN_LOG_WARN, "stack alloc failed for task %s, size=%zu",
                 task->name, stack_size);
        return;
    }

    /* 填充栈为已知模式（0xAA）用于使用量检测 */
    memset(task->stack_base, 0xAA, stack_size);
}

/*
 * 写入金丝雀到栈底（最低地址）。
 * 必须在 makecontext 之后调用，因为 makecontext 会向栈顶写设置数据。
 */
static void task_write_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return;
    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary = KERN_STACK_CANARY;
        memcpy(task->stack_base, &canary, sizeof(uint32_t));
    }
}
