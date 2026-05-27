/**
 * @file   kern_task.c
 * @brief  Xeros 协作式多任务调度器实现
 * @details 实现 Round-Robin 协作式调度器。
 *
 *          Native 测试：使用 POSIX ucontext（每任务独立栈 + swapcontext）
 *          ESP32：每个 Xeros 任务包装为一个 FreeRTOS 任务（独立栈），
 *                 通过互斥锁实现协作式调度。Round-Robin 由自定义调度器
 *                 管理，上下文切换委托 FreeRTOS（避免 Xtensa 窗口 ABI 冲突）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_task.h"
#include "kern_init.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef NATIVE_TEST
#include <ucontext.h>
#else
// ESP32: 使用 FreeRTOS 任务 + 二值信号量实现协作式调度
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

/* ═══ 内部常量 ═══ */

#define MAX_TASKS          KERN_MAX_TASKS
#define IDLE_STACK_MIN     2048   /* idle FreeRTOS 任务栈（需容纳 printf 调用） */
#define WRAPPER_STACK_MIN  4096   /* FreeRTOS 任务包装器栈大小 */

/* ═══ 全局调度状态 ═══ */

static kern_task_t   *g_task_list = NULL;      /* 任务链表头 */
static kern_task_t   *g_current_task = NULL;   /* 当前运行的任务 */
static kern_task_t   *g_idle_task = NULL;      /* idle 任务（兜底） */
static kern_task_t   *g_last_picked = NULL;    /* 上一轮选中的任务（Round-Robin 扫描起点） */
static kern_pid_t     g_next_pid = 0;          /* 下一个分配的 PID */
static uint8_t        g_task_count = 0;        /* 任务总数 */
static uint32_t       g_sched_ticks = 0;       /* 调度 tick 计数 */
static bool           g_sched_initialized = false;

#ifdef NATIVE_TEST
/*
 * 调度器上下文
 */
static ucontext_t     g_sched_ctx;

/*
 * makecontext 参数传递：uc_mcontext 只能传 int 参数，
 * 64 位指针通过此全局变量传递。
 */
static kern_task_t   *g_switch_to_task = NULL;
#endif

/* ═══ 前向声明 ═══ */

static void idle_entry(void *arg);

#ifdef NATIVE_TEST
static kern_task_t *pick_next_ready(void);
static void task_stack_init(kern_task_t *task, size_t stack_size);
static void task_write_canary(kern_task_t *task);
static void task_entry_trampoline(void);
#endif

#ifndef NATIVE_TEST
static kern_task_t *pick_next_ready(void);
static void task_stack_init(kern_task_t *task, size_t stack_size);
static void task_write_canary(kern_task_t *task);
static void task_wrapper(void *arg);

/* FreeRTOS 协作调度：双信号量实现令牌传递
 *
 * 原设计使用单个二值信号量，调度器 give 后立即 take，
 * 在单核上没有阻塞窗口让任务运行，导致任务永远拿不到令牌。
 *
 * 新设计：
 *   g_token_sem  — CPU 令牌，任务获取后才能运行
 *   g_done_sem   — 任务完成信号，任务 yield 时通知调度器
 *
 * 流程：
 *   调度器: give(token) → take(done)   (阻塞等待任务完成)
 *   任务:   take(token) → 执行 → give(done) → yield → take(token)
 */
static SemaphoreHandle_t g_token_sem = NULL;       /* CPU 令牌 */
static SemaphoreHandle_t g_done_sem  = NULL;       /* 任务完成通知 */
static TaskHandle_t      g_scheduler_task = NULL;  /* 调度器所在的 FreeRTOS 任务 */
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

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器 + 协作锁) ═══════════════ */

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    /* 创建双信号量 */
    g_token_sem = xSemaphoreCreateBinary();
    configASSERT(g_token_sem != NULL);
    g_done_sem = xSemaphoreCreateBinary();
    configASSERT(g_done_sem != NULL);
    /* 两个信号量初始 count 均为 0，调度器持有概念上的令牌 */

    /* 记录调度器所在 FreeRTOS 任务（即 main loop 的任务） */
    g_scheduler_task = xTaskGetCurrentTaskHandle();

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

    /* idle 任务也需要 FreeRTOS 任务承载 */
    BaseType_t ret = xTaskCreate(
        task_wrapper,           /* 包装函数 */
        "xidle",                /* FreeRTOS 任务名 */
        IDLE_STACK_MIN,         /* 栈大小 */
        g_idle_task,            /* 参数 = kern_task_t* */
        tskIDLE_PRIORITY,       /* 最低优先级 */
        &g_idle_task->rtos_handle
    );
    if (ret != pdPASS) {
        kern_panic("failed to create idle FreeRTOS task");
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

    kern_task_t *volatile next = pick_next_ready();
    if (next == NULL) return;

    g_current_task = next;
    g_switch_to_task = next;
    swapcontext(&g_sched_ctx, &next->ctx);
}

#else /* ESP32: 协作式 tick，释放令牌给下一个任务 */

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;
    if (g_token_sem == NULL || g_done_sem == NULL) return;

    g_sched_ticks++;

    kern_task_t *next = pick_next_ready();
    
    if (next == NULL) {
        /* 无就绪任务，短暂让出 CPU 让 FreeRTOS 处理其它事务 */
        if (g_sched_ticks % 1000 == 0) {
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        }
        vTaskDelay(1);
        return;
    }

    g_current_task = next;
    if (next->state != KERN_TASK_SLEEPING) {
        next->state = KERN_TASK_RUNNING;
    }

    /*
     * 双信号量协议：
     *   1. give(token) — 把 CPU 令牌交给选中的任务
     *   2. take(done)  — 阻塞等待任务 yield 时归还令牌
     *
     * 任务侧（kern_yield / task_wrapper）：
     *   take(token) → 执行 → give(done) → yield → take(token)
     */
    if (xSemaphoreGive(g_token_sem) != pdTRUE) {
        kern_log(KERN_LOG_ERROR, "sched: give token failed for task %d (%s)",
                 next->pid, next->name);
        return;
    }

    /* 等待任务完成（5 秒超时防止崩溃导致死锁） */
    if (xSemaphoreTake(g_done_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        kern_log(KERN_LOG_ERROR, "sched: task %d (%s) timeout - marking ZOMBIE",
                 next->pid, next->name);
        next->state = KERN_TASK_ZOMBIE;
        g_current_task = g_idle_task;
    }
}

#endif

/* ═══ 任务创建 ═══ */

#ifdef NATIVE_TEST

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    if (!g_sched_initialized) {
        kern_sched_init();
    }

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->entry = entry;
    task->arg = arg;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_STACK_MIN;
    task_stack_init(task, stack_sz);

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

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器) ═══════════════ */

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    if (!g_sched_initialized) {
        kern_sched_init();
    }

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->entry = entry;
    task->arg = arg;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    /* 创建 FreeRTOS 任务承载此 Xeros 任务 */
    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)WRAPPER_STACK_MIN;
    if (stack_sz < WRAPPER_STACK_MIN) stack_sz = WRAPPER_STACK_MIN;

    /* 记录栈信息用于调试 */
    task->stack_size = stack_sz;

    BaseType_t ret = xTaskCreate(
        task_wrapper,           /* 包装函数 */
        name ? name : "xtask",  /* FreeRTOS 任务名 */
        (uint32_t)stack_sz,     /* 栈大小（字） */ 
        task,                   /* 参数 = kern_task_t* */
        tskIDLE_PRIORITY + 1,   /* 优先级略高于 idle */
        &task->rtos_handle
    );
    if (ret != pdPASS) {
        kern_log(KERN_LOG_WARN, "FreeRTOS task create failed for %s", task->name);
        free(task);
        return KERN_ENOMEM;
    }

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

#endif

/* ═══ Yield ═══ */

#ifdef NATIVE_TEST

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_READY;
    g_current_task = g_idle_task;

    swapcontext(&cur->ctx, &g_sched_ctx);
}

#else /* ESP32: yield = 释放调度锁，让调度器选择下一任务 */

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_READY;

    /*
     * 双信号量协议：
     *   1. give(done) — 通知调度器本任务已完成当前时间片
     *   2. take(token) — 等待调度器再次分配 CPU 令牌
     */
    xSemaphoreGive(g_done_sem);
    xSemaphoreTake(g_token_sem, portMAX_DELAY);

    /* 被唤醒：调度器已把我们设为 RUNNING */
}

#endif

/* ═══ Exit ═══ */

#ifdef NATIVE_TEST

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;
    setcontext(&g_sched_ctx);
}

#else /* ESP32: exit = 将任务标记为 ZOMBIE，通知调度器，删除自身 */

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;

    /* 通知调度器任务结束 */
    xSemaphoreGive(g_done_sem);

    /* 删除当前 FreeRTOS 任务（不会返回） */
    vTaskDelete(NULL);
}

#endif

/* ═══ Sleep ═══ */

#ifdef NATIVE_TEST

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;
    g_current_task = g_idle_task;

    swapcontext(&cur->ctx, &g_sched_ctx);
}

#else /* ESP32: sleep = yield + 记录唤醒时间 */

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;

    /* yield 到调度器，调度器会检查 wake_time 决定是否重新调度此任务 */
    kern_yield();
}

#endif

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

#ifdef NATIVE_TEST

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;

    #define CANARY_SKIP 8
    size_t scan_start = (task->stack_size > CANARY_SKIP) ?
                        (size_t)CANARY_SKIP : task->stack_size;
    size_t used = 0;
    for (size_t i = scan_start; i < task->stack_size; i++) {
        if (task->stack_base[i] != 0xAA) {
            used = task->stack_size - i;
            break;
        }
    }

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

#else /* ESP32: FreeRTOS 管理栈，无法直接扫描 0xAA */

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL) return 0;
    /* FreeRTOS 提供高水位线查询 */
    if (task->rtos_handle != NULL) {
        UBaseType_t high_water = uxTaskGetStackHighWaterMark(task->rtos_handle);
        /* 高水位线 = 剩余字数，总栈 - 剩余 = 使用量 */
        size_t total_words = task->stack_size / sizeof(StackType_t);
        if (high_water <= total_words) {
            return (total_words - high_water) * sizeof(StackType_t);
        }
    }
    return 0;
}

uint32_t kern_task_stack_canary(kern_task_t *task)
{
    /* FreeRTOS 任务不用 canary，返回 0 表示无检测 */
    (void)task;
    return 0;
}

#endif

/* ═══ 内部函数（Native） ═══ */

#ifdef NATIVE_TEST

static void idle_entry(void *arg)
{
    (void)arg;
    while (1) {
        kern_yield();
    }
}

static void task_entry_trampoline(void)
{
    kern_task_t *task = g_switch_to_task;

    if (task == NULL || task->entry == NULL) {
        setcontext(&g_sched_ctx);
        return;
    }

    task->entry(task->arg);

    task->state = KERN_TASK_ZOMBIE;
    g_current_task = g_idle_task;
    setcontext(&g_sched_ctx);
}

static kern_task_t *pick_next_ready(void)
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

    memset(task->stack_base, 0xAA, stack_size);
}

static void task_write_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return;
    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary = KERN_STACK_CANARY;
        memcpy(task->stack_base, &canary, sizeof(uint32_t));
    }
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器) ═══════════════ */

static void idle_entry(void *arg)
{
    (void)arg;
    while (1) {
        kern_yield();
    }
}

/*
 * ─── FreeRTOS 任务包装器 ───
 *
 * 每个 Xeros 任务包装在一个 FreeRTOS 任务中。
 * 使用双信号量协议与调度器同步：
 *   take(token) → 执行任务 → (任务内部 yield 时) give(done)/take(token)
 *
 * 首次运行时，任务阻塞在 take(token) 上，直到调度器分配令牌。
 */
static void task_wrapper(void *arg)
{
    kern_task_t *task = (kern_task_t *)arg;

    /* 等待调度器给我们令牌（首次运行或 yield 后恢复） */
    xSemaphoreTake(g_token_sem, portMAX_DELAY);

    /* 获得了令牌：现在是我们运行的时间片 */
    task->state = KERN_TASK_RUNNING;
    g_current_task = task;

    /* 执行任务入口 */
    if (task->entry != NULL) {
        task->entry(task->arg);
    }

    /* 入口返回：任务结束 */
    task->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", task->pid, task->name);
    g_current_task = g_idle_task;

    /* 归还令牌（通过 done 信号量） */
    xSemaphoreGive(g_done_sem);

    /* 删除自身（不会返回） */
    vTaskDelete(NULL);
}

/* ─── Round-Robin 就绪任务选择 ─── */

static kern_task_t *pick_next_ready(void)
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

/* ─── 栈初始化（空实现：FreeRTOS 管理栈）─── */
static void task_stack_init(kern_task_t *task, size_t stack_size)
{
    (void)task;
    (void)stack_size;
    /* FreeRTOS 分配和管理任务栈 */
}

static void task_write_canary(kern_task_t *task)
{
    (void)task;
    /* FreeRTOS 有自己的栈溢出检测 */
}

#endif
