# Xeros 可移植层（Port Layer）

> **Parent:** [内核子系统](index.md) | **Related:** [原生内核架构](../architecture/xeros-native-kernel.md) | [调度器设计](../architecture/scheduler.md)

## 概述

Xeros 通过 `kern_port_ops_t` 操作表把底层执行上下文（线程创建、上下文切换、定时器、空闲处理）抽象出来，使内核其余代码不必直接依赖 FreeRTOS 或具体硬件。

*📄 Source: [kern_port.h](../../src/kernel/kern_port.h#L64-L88)*

```c
typedef struct kern_port_ops {
    void  (*init)(void);

    kern_port_thread_t (*thread_spawn)(
        void (*entry)(void*), void *arg, const char *name,
        size_t stack_size, struct kern_task *task);
    void  (*thread_exit)(void);
    void  (*thread_kill)(kern_port_thread_t);
    size_t (*thread_stack_usage)(kern_port_thread_t);

    void  (*switch_to)(struct kern_task *task);
    void  (*task_yield)(void);
    void  (*task_exit)(void);

    void  (*idle)(void);

    kern_err_t (*timer_set_periodic)(uint32_t period_us);
    void       (*timer_stop)(void);
    bool       (*preempt_consume)(void);
} kern_port_ops_t;
```

## 定时器基础设施

### `kern_port_timer_set_periodic()`

启动周期性硬件定时器，为抢占式调度提供 ISR tick 源。

*📄 Source: [kern_port.h](../../src/kernel/kern_port.h#L206-L216)*

```c
kern_err_t kern_port_timer_set_periodic(uint32_t period_us);
```

**参数与返回值：**

- `period_us`：定时周期，单位微秒，**必须大于 0**。
- 返回值：
  - `KERN_OK`：成功启动（或定时器已在运行）。
  - `KERN_EINVAL`：`period_us == 0`。
  - `KERN_ERR`：GPTimer 初始化/启动失败。

**后端实现：**

- FreeRTOS 后端（`m5stick-c`）：`kern_port_freertos_timer_set()`，使用 ESP-IDF GPTimer。
  *📄 Source: [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L75-L135)*
- 原生调度器后端（`m5stick-c-native`）：`native_timer_set()`，使用 `tick_timer` 模块。
  *📄 Source: [kern_port_esp32_native.c](../../src/kernel/kern_port_esp32_native.c#L223-L236)*
- Native 测试桩：`kern_port_native_test_timer_set()`，用于 `pio test -e native`。
  *📄 Source: [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L419-L420)*

### `kern_port_timer_stop()` 与 `kern_port_preempt_consume()`

- `kern_port_timer_stop()`：停止并释放硬件定时器。
- `kern_port_preempt_consume()`：在任务上下文中检查并消费 ISR 设置的抢占标志；返回 `true` 时表示需要触发一次 `kern_sched_tick()`。

## 相关测试

- `KernelPortTest.TimerSetPeriodicReturnsOk`
- `KernelPortTest.TimerSetPeriodicRejectsZeroPeriod`

*📄 Source: [test_kernel_sched.cpp](../../../test/test_native/test_kernel_sched.cpp#L486-L497)*

---

> **See Also:** [kern_port.h](../../src/kernel/kern_port.h) | [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c) | [kern_port_esp32_native.c](../../src/kernel/kern_port_esp32_native.c)
