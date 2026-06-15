/**
 * @file   kern_port_freertos.c
 * @brief  Xeros 内核可移植层 — FreeRTOS 后端实现
 * @details 基于 FreeRTOS 任务容器 + 双信号量令牌协议实现抢占式调度。
 *
 *          协议：
 *          ┌──────────┐    give(token)     ┌──────────┐
 *          │ Scheduler │ ────────────────→ │   Task   │
 *          │ (loop)    │ ←──────────────── │ (wrapper)│
 *          └──────────┘    take(done)      └──────────┘
 *
 *          每个 Xeros 任务 = 1 个 FreeRTOS 任务。
 *          任务切换通过双二值信号量实现，硬件定时器驱动抢占式调度。
 *
 *          此文件是项目中唯一直接调用 FreeRTOS API 的源文件。
 *          切换到原生调度器时，替换此文件为 kern_port_native.c 即可。
 *
 *          通过 kern_port_ops_t 结构体提供后端多态：
 *          所有公共函数以 _freertos / _native_sched / _native_test 后缀
 *          命名，由 g_kern_port_ops 表统一分派。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_port.h"
#include "kern_task.h"
#include "kern_smp.h"
#include "kern_init.h"

#include <string.h>
#include <stdlib.h>

#ifndef NATIVE_TEST

#if defined(XEROS_NATIVE_SCHED)

/* ═══ XEROS_NATIVE_SCHED 桩：调度逻辑在 kern_task.c 中实现 ═══ */

static void kern_port_native_sched_init(void) {}

static kern_port_thread_t kern_port_native_sched_thread_spawn(
    void (*entry)(void *arg), void *arg, const char *name,
    size_t stack_size, kern_task_t *task)
{ (void)entry; (void)arg; (void)name; (void)stack_size; (void)task;
  return KERN_PORT_THREAD_NULL; }

static void kern_port_native_sched_thread_exit(void) { while (1) {} }

static void kern_port_native_sched_thread_kill(kern_port_thread_t thread) { (void)thread; }

static size_t kern_port_native_sched_thread_stack_usage(kern_port_thread_t thread)
{ (void)thread; return 0; }

static void kern_port_native_sched_switch_to(kern_task_t *task) { (void)task; }

static void kern_port_native_sched_task_yield(void) {}

static void kern_port_native_sched_task_exit(void) { while (1) {} }

static void kern_port_native_sched_idle(void)
{
    /* 无就绪任务时短暂忙等待让出 CPU */
    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }
}

static int kern_port_native_sched_timer_set(uint32_t period_us)
{ (void)period_us; return 0; }

static void kern_port_native_sched_timer_stop(void) {}

static bool kern_port_native_sched_preempt_consume(void) { return false; }

const kern_port_ops_t g_kern_port_ops = {
    .init                = kern_port_native_sched_init,
    .thread_spawn        = kern_port_native_sched_thread_spawn,
    .thread_exit         = kern_port_native_sched_thread_exit,
    .thread_kill         = kern_port_native_sched_thread_kill,
    .thread_stack_usage  = kern_port_native_sched_thread_stack_usage,
    .switch_to           = kern_port_native_sched_switch_to,
    .task_yield          = kern_port_native_sched_task_yield,
    .task_exit           = kern_port_native_sched_task_exit,
    .idle                = kern_port_native_sched_idle,
    .timer_set_periodic  = kern_port_native_sched_timer_set,
    .timer_stop          = kern_port_native_sched_timer_stop,
    .preempt_consume     = kern_port_native_sched_preempt_consume,
};

#else /* FreeRTOS 后端（默认）*/

/* FreeRTOS 头文件（仅此文件直接依赖） */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifdef CONFIG_PREEMPT_ENABLED
#include <driver/timer.h>
#include <esp_attr.h>
#endif

/* ═══ 内部状态 ═══ */

static SemaphoreHandle_t g_token_sem[KERN_MAX_CPUS];  /* 每核 CPU 令牌 */
static SemaphoreHandle_t g_done_sem[KERN_MAX_CPUS];   /* 每核任务完成通知 */

#ifdef CONFIG_PREEMPT_ENABLED
static volatile bool g_preempt_tick_pending = false;  /* ISR → loop() 通知标志 */
static volatile bool g_timer_active = false;

/*
 * ESP32 硬件定时器 ISR（timer_group0, timer0, 1ms 周期）
 *
 * ═══ ISR 安全原则 ═══
 * 本 ISR 仅执行最小化工作：设置抢占标志。
 * 所有调度逻辑（reap_zombies、pick_next、switch_to）在 loop() 任务上下文执行。
 * 严禁在 ISR 中调用：① xSemaphoreTake（阻塞）；② free/malloc；
 * ③ 不可预测长度的链表遍历；④ 非 FromISR 的 FreeRTOS API。
 */
static bool IRAM_ATTR sched_timer_isr(void *arg)
{
    (void)arg;
    g_preempt_tick_pending = true;

    /* 清除中断标志 */
    TIMERG0.int_clr_timers.t0 = 1;
    TIMERG0.hw_timer[0].update = 1;
    return true;
}

static int kern_port_freertos_timer_set(uint32_t period_us)
{
    if (g_timer_active) return 0;  /* 已启动 */

    g_preempt_tick_pending = false;

    timer_config_t config = {
        .divider     = 80,   /* 80MHz / 80 = 1MHz → 1 tick = 1us */
        .counter_dir = TIMER_COUNT_UP,
        .counter_en  = TIMER_PAUSE,
        .alarm_en    = TIMER_ALARM_EN,
        .auto_reload = true,
        .intr_type   = TIMER_INTR_LEVEL,
    };

    if (timer_init(TIMER_GROUP_0, TIMER_0, &config) != ESP_OK) {
        return -1;
    }

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, period_us);

    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, sched_timer_isr, NULL, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);
    g_timer_active = true;

    return 0;
}

static void kern_port_freertos_timer_stop(void)
{
    if (!g_timer_active) return;
    timer_pause(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_remove(TIMER_GROUP_0, TIMER_0);
    g_timer_active = false;
    g_preempt_tick_pending = false;
}

/*
 * @brief 检查并消费抢占 tick 请求（loop() 上下文调用）
 * @return true 如果有待处理的抢占 tick，同时清零标志
 */
static bool kern_port_freertos_preempt_consume(void)
{
    if (!g_preempt_tick_pending) return false;
    g_preempt_tick_pending = false;
    return true;
}
#else
static int kern_port_freertos_timer_set(uint32_t period_us)
{ (void)period_us; return 0; }

static void kern_port_freertos_timer_stop(void) {}

static bool kern_port_freertos_preempt_consume(void) { return false; }

#endif /* CONFIG_PREEMPT_ENABLED */

/* ═══ 任务包装器 ═══ */

/**
 * @brief FreeRTOS 任务包装器
 * @note  每个 Xeros 任务在一个独立的 FreeRTOS 任务中运行。
 *        首次运行时阻塞在 take(token) 上，等待调度器分配令牌。
 *
 *        协议：
 *          take(token) → 执行 entry() → give(done) → vTaskDelete(NULL)
 */
static void task_wrapper(void *arg)
{
    kern_task_t *task = (kern_task_t *)arg;
    uint8_t cpu = task->cpu_id;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;

    /* 等待本任务所属 CPU 的调度器给我们令牌（首次运行或 yield 后恢复） */
    xSemaphoreTake(g_token_sem[cpu], portMAX_DELAY);

    /* 获得了令牌：现在是我们运行的时间片 */
    task->state = KERN_TASK_RUNNING;
    /* g_current_task 由 kern_task.c 维护 */

    /* 执行任务入口 */
    if (task->entry != NULL) {
        task->entry(task->arg);
    }

    /* 入口返回：任务结束；统一走 kern_exit() 释放资源并退出 */
    kern_exit();

    /* kern_exit() 不会返回 */
}

/* ═══ 生命周期 ═══ */

static void kern_port_freertos_init(void)
{
    if (g_token_sem[0] != NULL) return;  /* 已初始化 */

    for (uint8_t i = 0; i < KERN_MAX_CPUS; i++) {
        g_token_sem[i] = xSemaphoreCreateBinary();
        configASSERT(g_token_sem[i] != NULL);
        g_done_sem[i] = xSemaphoreCreateBinary();
        configASSERT(g_done_sem[i] != NULL);
    }
    /* 所有信号量初始 count=0，调度器持有概念上的令牌 */
}

/* ═══ 线程管理 ═══ */

static kern_port_thread_t kern_port_freertos_thread_spawn(
    void (*entry)(void *arg),
    void *arg,
    const char *name,
    size_t stack_size,
    kern_task_t *task)
{
    (void)entry;  /* FreeRTOS 后端始终使用内部 task_wrapper，实际入口从 task->entry 读取 */
    (void)arg;    /* 同上，参数通过 TCB 传递 */

    if (task == NULL) return KERN_PORT_THREAD_NULL;

    if (stack_size < KERN_PORT_STACK_MIN) {
        stack_size = KERN_PORT_STACK_MIN;
    }

    TaskHandle_t handle = NULL;
    uint8_t cpu = task->cpu_id;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_wrapper,           /* 包装函数 */
        name ? name : "xtask",  /* FreeRTOS 任务名 */
        (uint32_t)stack_size,   /* 栈大小（字） */
        task,                   /* 参数 = kern_task_t* */
        tskIDLE_PRIORITY + 1,   /* 优先级略高于 idle */
        &handle,
        cpu                     /* 引脚到任务分配的 CPU */
    );

    if (ret != pdPASS) {
        kern_log(KERN_LOG_WARN, "port: xTaskCreate failed for %s", name);
        return KERN_PORT_THREAD_NULL;
    }

    return (kern_port_thread_t)handle;
}

static void kern_port_freertos_thread_exit(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;
    /*
     * 通知调度器任务结束，然后删除当前 FreeRTOS 任务。
     * 注意：此函数不会返回。
     */
    xSemaphoreGive(g_done_sem[cpu]);
    vTaskDelete(NULL);

    /* 不会到达这里，但让编译器满意 */
    while (1) {}
}

static void kern_port_freertos_thread_kill(kern_port_thread_t thread)
{
    if (thread == KERN_PORT_THREAD_NULL) return;
    vTaskDelete((TaskHandle_t)thread);
}

static size_t kern_port_freertos_thread_stack_usage(kern_port_thread_t thread)
{
    if (thread == KERN_PORT_THREAD_NULL) return 0;
    TaskHandle_t handle = (TaskHandle_t)thread;
    uint32_t high_water = uxTaskGetStackHighWaterMark(handle);
    /* FreeRTOS 返回剩余字数，转换为已使用字节数（近似） */
    (void)high_water;
    return 0;  /* 调用者从 TCB 获取 stack_size，此处返回 0 表示"由 FreeRTOS 管理" */
}

/* ═══ 上下文切换 ═══ */

static void kern_port_freertos_switch_to(kern_task_t *task)
{
    uint8_t cpu = KERN_THIS_CPU;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;

    (void)task;
    /*
     * 双信号量协议 — 调度器侧（每核独立）：
     *   1. give(token[cpu]) — 把本 CPU 令牌交给目标任务
     *   2. take(done[cpu])   — 阻塞等待任务 yield/exit 时归还
     */
    if (xSemaphoreGive(g_token_sem[cpu]) != pdTRUE) {
        kern_log(KERN_LOG_ERROR, "port: give token failed on cpu %d", cpu);
        return;
    }

    /* 等待任务完成（5 秒超时防止崩溃导致死锁） */
    if (xSemaphoreTake(g_done_sem[cpu], pdMS_TO_TICKS(5000)) != pdTRUE) {
        kern_log(KERN_LOG_ERROR, "port: task timeout on cpu %d - marking ZOMBIE", cpu);
        if (task != NULL) {
            task->state = KERN_TASK_ZOMBIE;
        }
    }
}

static void kern_port_freertos_task_yield(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;
    /*
     * 双信号量协议 — 任务侧（yield）：
     *   1. give(done[cpu]) — 通知本 CPU 调度器任务已完成当前时间片
     *   2. take(token[cpu]) — 等待调度器再次分配 CPU 令牌
     */
    xSemaphoreGive(g_done_sem[cpu]);
    xSemaphoreTake(g_token_sem[cpu], portMAX_DELAY);
    /* 被唤醒：调度器已把我们设为 RUNNING */
}

static void kern_port_freertos_task_exit(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;
    /*
     * 双信号量协议 — 任务侧（exit）：
     *   1. give(done[cpu]) — 通知本 CPU 调度器任务已退出
     *   2. vTaskDelete — 删除自身（不返回）
     */
    xSemaphoreGive(g_done_sem[cpu]);
    vTaskDelete(NULL);

    /* 不会到达 */
    while (1) {}
}

/* ═══ 空闲处理 ═══ */

static void kern_port_freertos_idle(void)
{
    /* 无就绪任务时短暂让出 CPU，让 FreeRTOS 处理 WiFi/BT 事务 */
    vTaskDelay(1);
}

const kern_port_ops_t g_kern_port_ops = {
    .init                = kern_port_freertos_init,
    .thread_spawn        = kern_port_freertos_thread_spawn,
    .thread_exit         = kern_port_freertos_thread_exit,
    .thread_kill         = kern_port_freertos_thread_kill,
    .thread_stack_usage  = kern_port_freertos_thread_stack_usage,
    .switch_to           = kern_port_freertos_switch_to,
    .task_yield          = kern_port_freertos_task_yield,
    .task_exit           = kern_port_freertos_task_exit,
    .idle                = kern_port_freertos_idle,
    .timer_set_periodic  = kern_port_freertos_timer_set,
    .timer_stop          = kern_port_freertos_timer_stop,
    .preempt_consume     = kern_port_freertos_preempt_consume,
};

#endif /* defined(XEROS_NATIVE_SCHED) */

#else /* NATIVE_TEST — 空桩：原生模式不使用此文件 */

static void kern_port_native_test_init(void) {}

static kern_port_thread_t kern_port_native_test_thread_spawn(
    void (*entry)(void *arg), void *arg, const char *name,
    size_t stack_size, kern_task_t *task)
{ (void)entry; (void)arg; (void)name; (void)stack_size; (void)task;
  return KERN_PORT_THREAD_NULL; }

static void kern_port_native_test_thread_exit(void) { while(1){} }

static void kern_port_native_test_thread_kill(kern_port_thread_t thread) { (void)thread; }

static size_t kern_port_native_test_thread_stack_usage(kern_port_thread_t thread)
{ (void)thread; return 0; }

static void kern_port_native_test_switch_to(kern_task_t *task) { (void)task; }

static void kern_port_native_test_task_yield(void) {}

static void kern_port_native_test_task_exit(void) { while(1){} }

static void kern_port_native_test_idle(void) {}

static int kern_port_native_test_timer_set(uint32_t period_us)
{ (void)period_us; return 0; }

static void kern_port_native_test_timer_stop(void) {}

static bool kern_port_native_test_preempt_consume(void) { return false; }

const kern_port_ops_t g_kern_port_ops = {
    .init                = kern_port_native_test_init,
    .thread_spawn        = kern_port_native_test_thread_spawn,
    .thread_exit         = kern_port_native_test_thread_exit,
    .thread_kill         = kern_port_native_test_thread_kill,
    .thread_stack_usage  = kern_port_native_test_thread_stack_usage,
    .switch_to           = kern_port_native_test_switch_to,
    .task_yield          = kern_port_native_test_task_yield,
    .task_exit           = kern_port_native_test_task_exit,
    .idle                = kern_port_native_test_idle,
    .timer_set_periodic  = kern_port_native_test_timer_set,
    .timer_stop          = kern_port_native_test_timer_stop,
    .preempt_consume     = kern_port_native_test_preempt_consume,
};

#endif /* NATIVE_TEST */
