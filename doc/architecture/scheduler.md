# 调度器与 Tick 定时器设计

> **Parent:** [原生内核架构](xeros-native-kernel.md) | **Related:** [上下文切换](context-switch.md)

## 概述

本文档描述原生后端的调度器和定时器设计。当前系统使用 ESP-IDF GPTimer 设置抢占标志，由主循环轮询触发调度。原生后端将使用硬件定时器直接驱动周期性 tick 中断，实现真正的抢占式调度。

## 当前 FreeRTOS 后端调度流程

*📄 Source: [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L121-L128)*

```
主循环 (loop):
    kern_port_preempt_consume()  ← 检查 ISR 标志
    if (有抢占请求) {
        kern_sched_tick()        ← 执行调度
    }
    vTaskDelay(1)                ← 让出 CPU

GPTimer ISR (1ms):
    g_preempt_tick_pending = true  ← 仅设置标志
```

**问题：** 依赖 FreeRTOS 的 `vTaskDelay` 让出 CPU，调度粒度受主循环频率限制。

## 原生后端设计

### Tick 定时器

使用 ESP32 Timer Group 0 (TIMG0) 作为 tick 源：

```c
// tick_timer.h
typedef struct {
    uint32_t      period_us;        // tick 周期（微秒）
    volatile uint32_t tick_count;   // tick 计数器
    volatile bool need_resched;     // 抢占请求标志
} xeros_tick_timer_t;

kern_err_t xeros_tick_timer_init(uint32_t period_us);
void       xeros_tick_timer_start(void);
void       xeros_tick_timer_stop(void);
void       xeros_tick_timer_reprogram(uint32_t next_wake_us);  // tickless idle
```

### Tick ISR

```c
// tick_timer.c
static void IRAM_ATTR tick_timer_isr(void *arg)
{
    xeros_tick_timer_t *timer = (xeros_tick_timer_t *)arg;

    // 最少量工作：递增计数、检查唤醒、设置标志
    timer->tick_count++;

    // 检查睡眠任务是否到期
    check_sleeping_tasks(timer->tick_count);

    // 设置抢占标志（不在 ISR 中执行调度）
    timer->need_resched = true;

    // 清除中断标志（由硬件自动完成）
}
```

### 周期性 Tick 中断配置

```c
kern_err_t xeros_tick_timer_init(uint32_t period_us)
{
    // 使用 ESP32 Timer Group 0
    timer_config_t config = {
        .divider = 80,              // 80MHz / 80 = 1MHz
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
    };

    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, period_us);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, tick_timer_isr,
                       NULL, ESP_INTR_FLAG_IRAM, NULL);

    return KERN_OK;
}
```

## 调度器循环

### 单核模式

```c
// kern_sched.c - XEROS_NATIVE_SCHED 路径
void kern_sched_loop(void)
{
    while (1) {
        kern_sched_tick();

        // 无就绪任务时进入低功耗等待
        if (无就绪任务()) {
            __asm__ volatile("waiti 0");  // 等待中断
        }
    }
}
```

### SMP 双核模式

```
Core 0 (PRO_CPU)                    Core 1 (APP_CPU)
┌─────────────────────────┐        ┌─────────────────────────┐
│ while(1) {              │        │ while(1) {              │
│   kern_sched_tick();    │        │   kern_sched_tick();    │
│   if (idle) waiti 0;    │        │   if (idle) waiti 0;    │
│ }                       │        │ }                       │
└─────────────────────────┘        └─────────────────────────┘
         │                                    │
         └──── IPI (跨核唤醒) ────────────────┘
```

### kern_sched_tick 详细流程

```c
void kern_sched_tick(void)
{
    uint8_t cpu = KERN_THIS_CPU;

    // 1. 回收僵尸任务
    reap_zombies();

    // 2. 唤醒到期的睡眠任务
    wake_expired_sleepers();

    // 3. 检查是否有更高优先级任务就绪
    kern_task_t *next = pick_next_ready();
    if (next == NULL) {
        next = g_idle_task;  // 运行空闲任务
    }

    // 4. 如果需要切换
    if (next != g_current_task) {
        kern_task_t *prev = g_current_task;
        g_current_task = next;
        next->state = KERN_TASK_RUNNING;

        if (prev->state == KERN_TASK_RUNNING) {
            prev->state = KERN_TASK_READY;
        }

        // 5. 执行上下文切换
        // 保存调度器上下文，恢复目标任务上下文
        if (xeros_ctx_save(&g_sched_ctx[cpu]) == 0) {
            xeros_ctx_restore(next->native_ctx);
        }
        // 返回到这里说明目标任务 yield/exit 了
    }

    // 6. 更新调度统计
    g_sched_ticks++;
}
```

## 抢占式调度

### 抢占触发流程

```
Tick ISR (每 1ms):
    │
    ├── g_sched_ticks++
    ├── 检查睡眠任务
    └── g_need_resched = true

调度器循环:
    │
    ├── kern_sched_tick()
    │   └── pick_next_ready() 选择最高优先级任务
    └── 如果 next != current → 上下文切换
```

### 时间片轮转 (Round Robin)

```c
// kern_sched_rr.c
kern_task_t *rr_pick_next(void)
{
    kern_task_t *start = g_last_picked;
    kern_task_t *task = start->next;

    while (task != start) {
        if (task == NULL) task = g_task_list;
        if (task->state == KERN_TASK_READY &&
            task->scheduler_class_id == SCHED_CLASS_RR) {
            g_last_picked = task;
            return task;
        }
        task = task->next;
    }

    // 当前任务时间片用完，重置
    g_current_task->timeslice_remaining = RR_TIMESLICE;
    return NULL;
}
```

### 优先级 FIFO

```c
// kern_sched_fifo.c
kern_task_t *fifo_pick_next(void)
{
    kern_task_t *best = NULL;
    kern_task_t *task = g_task_list;

    while (task != NULL) {
        if (task->state == KERN_TASK_READY &&
            task->scheduler_class_id == SCHED_CLASS_FIFO) {
            if (best == NULL || task->priority > best->priority) {
                best = task;
            }
        }
        task = task->next;
    }

    return best;
}
```

## 睡眠管理

### kern_sleep_ms 实现

```c
void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *task = g_current_task;
    task->state = KERN_TASK_SLEEPING;
    task->wake_time = g_sched_ticks + (ms * TICKS_PER_MS);

    // 保存上下文，切换回调度器
    if (xeros_ctx_save(task->native_ctx) == 0) {
        xeros_ctx_restore(&g_sched_ctx[KERN_THIS_CPU]);
    }
    // 被唤醒后从这里继续
}
```

### 唤醒检查（在 tick ISR 中）

```c
static void check_sleeping_tasks(uint32_t current_tick)
{
    kern_task_t *task = g_task_list;
    while (task != NULL) {
        if (task->state == KERN_TASK_SLEEPING &&
            current_tick >= task->wake_time) {
            task->state = KERN_TASK_READY;
        }
        task = task->next;
    }
}
```

## Tickless Idle 模式

当系统空闲时，可以延长 tick 间隔以节省功耗：

```c
// 计算下一个唤醒时间
uint32_t next_wake = calculate_next_wake_time();

if (next_wake > current_tick + MIN_IDLE_TICKS) {
    // 重新编程定时器
    uint32_t idle_ticks = next_wake - current_tick;
    xeros_tick_timer_reprogram(idle_ticks * TICK_PERIOD_US);

    // 进入深度睡眠
    __asm__ volatile("waiti 0");

    // 醒来后恢复定时器
    xeros_tick_timer_reprogram(TICK_PERIOD_US);
}
```

## ESP32 特定考虑

### Timer Group 选择

| Timer Group | 用途 | 说明 |
|-------------|------|------|
| TIMG0 | Xeros tick | 专用，不与 ESP-IDF 冲突 |
| TIMG1 | 可用 | 可用于其他定时需求 |
| GPTimer | 已用于 FreeRTOS 后端 | 原生后端不再需要 |

### 中断优先级

```
Level 1: Tick 定时器 ISR (最低，可被更高优先级中断抢占)
Level 2-3: 外设中断 (UART, SPI 等)
Level 4-5: 高优先级中断 (时间关键)
Level 7: NMI (不可屏蔽)
```

Tick ISR 使用 Level 1，允许被外设中断抢占。

### WDT 喂狗

ESP32 的 Task WDT 需要在空闲时喂狗。原生后端的 idle 任务负责此操作：

```c
void idle_entry(void *arg)
{
    while (1) {
        // 喂狗
        esp_task_wdt_reset();

        // 低功耗等待
        __asm__ volatile("waiti 0");
    }
}
```

## 验证策略

### 定时器精度测试

```c
// 启动定时器，运行 10000 个 tick
// 使用外部时间源（ESP32 高分辨率定时器）测量
// 验证误差 < 0.1%
```

### 抢占测试

```c
// 创建高优先级任务，执行长时间计算
// 创建低优先级任务，执行循环
// 验证高优先级任务在 tick 边界抢占低优先级任务
```

### 睡眠精度测试

```c
// 任务调用 kern_sleep_ms(1000)
// 记录实际唤醒时间
// 验证误差 < 10ms (1% of 1000ms)
```

---

> **See Also:** [上下文切换](context-switch.md) | [原生内核架构](xeros-native-kernel.md)
