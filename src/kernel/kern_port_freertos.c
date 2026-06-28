/**
 * @file   kern_port_freertos.c
 * @brief  Xeros 内核可移植层 — FreeRTOS 后端实现（已废弃）
 * @details ⚠️ DEPRECATED: 此文件仅在 m5stick-c（非原生调度器）环境下使用。
 *          默认构建目标已切换为 m5stick-c-native（XEROS_NATIVE_SCHED），
 *          此文件不再参与编译。保留作为回退兼容参考。
 *          新代码请参考 kern_port_esp32_native.c（原生调度器后端）。
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

#if !defined(NATIVE_TEST) && !defined(XEROS_NATIVE_SCHED)

/* ═══ FreeRTOS 后端 ═══ */

/* FreeRTOS 头文件（仅此文件直接依赖） */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifdef CONFIG_PREEMPT_ENABLED
#include <driver/gptimer.h>
#include <esp_attr.h>
#endif

/* ═══ 内部状态 ═══ */

static SemaphoreHandle_t g_token_sem[KERN_MAX_CPUS];  /* 每核 CPU 令牌 */
static SemaphoreHandle_t g_done_sem[KERN_MAX_CPUS];   /* 每核任务完成通知 */

#ifdef CONFIG_PREEMPT_ENABLED
static volatile bool g_preempt_tick_pending = false;  /* ISR → loop() 通知标志 */
static volatile bool g_timer_active = false;
static gptimer_handle_t g_sched_timer = NULL;

/*
 * ESP32 GPTimer ISR（1ms 周期）
 *
 * ═══ ISR 安全原则 ═══
 * 本 ISR 仅执行最小化工作：设置抢占标志。
 * 所有调度逻辑（reap_zombies、pick_next、switch_to）在 loop() 任务上下文执行。
 * 严禁在 ISR 中调用：① xSemaphoreTake（阻塞）；② free/malloc；
 * ③ 不可预测长度的链表遍历；④ 非 FromISR 的 FreeRTOS API。
 */
static bool IRAM_ATTR sched_timer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *arg)
{
    (void)timer;
    (void)edata;
    (void)arg;
    g_preempt_tick_pending = true;
    return false;
}

static kern_err_t kern_port_freertos_timer_set(uint32_t period_us)
{
    if (period_us == 0) return KERN_EINVAL;
    if (g_timer_active) return KERN_OK;  /* 已启动 */

    g_preempt_tick_pending = false;

    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  /* 1MHz → 1 tick = 1us */
        .intr_priority = 0,
        .flags = {
            .intr_shared = 0,
        },
    };

    if (gptimer_new_timer(&timer_config, &g_sched_timer) != ESP_OK) {
        return KERN_ERR;
    }

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = period_us,
        .reload_count = 0,
        .flags = {
            .auto_reload_on_alarm = true,
        },
    };

    if (gptimer_set_alarm_action(g_sched_timer, &alarm_config) != ESP_OK) {
        gptimer_del_timer(g_sched_timer);
        g_sched_timer = NULL;
        return KERN_ERR;
    }

    gptimer_event_callbacks_t cbs = {
        .on_alarm = sched_timer_isr,
    };

    if (gptimer_register_event_callbacks(g_sched_timer, &cbs, NULL) != ESP_OK) {
        gptimer_del_timer(g_sched_timer);
        g_sched_timer = NULL;
        return KERN_ERR;
    }

    if (gptimer_enable(g_sched_timer) != ESP_OK) {
        gptimer_del_timer(g_sched_timer);
        g_sched_timer = NULL;
        return KERN_ERR;
    }

    if (gptimer_start(g_sched_timer) != ESP_OK) {
        gptimer_disable(g_sched_timer);
        gptimer_del_timer(g_sched_timer);
        g_sched_timer = NULL;
        return KERN_ERR;
    }

    g_timer_active = true;
    return KERN_OK;
}

static void kern_port_freertos_timer_stop(void)
{
    if (!g_timer_active || g_sched_timer == NULL) return;
    gptimer_stop(g_sched_timer);
    gptimer_disable(g_sched_timer);
    gptimer_del_timer(g_sched_timer);
    g_sched_timer = NULL;
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
static kern_err_t kern_port_freertos_timer_set(uint32_t period_us)
{ (void)period_us; return KERN_OK; }

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

    /* stack_size 为字节，转换为 FreeRTOS 需要的字数（向上对齐） */
    size_t stack_words = stack_size / sizeof(StackType_t);
    if (stack_size % sizeof(StackType_t) != 0) {
        stack_words++;
    }

    /* 约束最小字数，避免低于 FreeRTOS 绝对下限 */
    if (stack_words < 1) stack_words = 1;

    TaskHandle_t handle = NULL;
    uint8_t cpu = task->cpu_id;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;

    /*
     * 优先级分层：
     *   - idle 任务 (xidleN): tskIDLE_PRIORITY，只在所有 Xeros 任务阻塞时运行，
     *     确保 FreeRTOS idle 任务能喂中断看门狗 (INT_WDT)。
     *   - 普通 Xeros 任务: tskIDLE_PRIORITY + 1，避免被 FreeRTOS RR 切走。
     *   - UI 任务: tskIDLE_PRIORITY + 2，保证一帧渲染能连续运行到 yield。
     *
     * 调度器任务 (app_main / kern_smp_sched_loop) 设为 tskIDLE_PRIORITY + 1，
     * 与 Xeros 普通任务同级；它大部分时间阻塞在 done_sem 或 vTaskDelay 上，
     * 不会长时间占用 CPU。UI 优先级更高，因此 1ms tick 不会抢占 UI。
     */
    UBaseType_t freertos_prio = tskIDLE_PRIORITY + 1;
    if (name != NULL && strncmp(name, "xidle", 5) == 0) {
        freertos_prio = tskIDLE_PRIORITY;
    } else if (name != NULL && strcmp(name, "ui") == 0) {
        freertos_prio = tskIDLE_PRIORITY + 2;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_wrapper,           /* 包装函数 */
        name ? name : "xtask",  /* FreeRTOS 任务名 */
        (uint32_t)stack_words,  /* 栈大小（字） */
        task,                   /* 参数 = kern_task_t* */
        freertos_prio,          /* FreeRTOS 优先级 */
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
    UBaseType_t high_water = uxTaskGetStackHighWaterMark(handle);
    /* 返回剩余栈字数（供调用者乘以 sizeof(StackType_t) 转换为字节） */
    return (size_t)high_water;
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

#elif defined(NATIVE_TEST) /* 空桩：原生桌面测试不使用 FreeRTOS */

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

static kern_err_t kern_port_native_test_timer_set(uint32_t period_us)
{ return (period_us == 0) ? KERN_EINVAL : KERN_OK; }

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
