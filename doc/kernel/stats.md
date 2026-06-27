# 运行时统计与看门狗

> **Parent:** [Xeros 内核文档](index.md)

本模块提供任务级 CPU 占用统计、任务看门狗和栈溢出检测。

## 核心 API

*📄 Source: [kern_stats.h](../../src/kernel/kern_stats.h#L11-L22)*

```c
void     kern_stats_init(void);
void     kern_stats_task_start(kern_task_t *task);
void     kern_stats_task_stop(kern_task_t *task);
void     kern_stats_update(void);
uint64_t kern_stats_get_runtime_us(const kern_task_t *task);
uint8_t  kern_stats_get_cpu_percent(const kern_task_t *task);

kern_err_t kern_watchdog_register(kern_task_t *task);
kern_err_t kern_watchdog_feed(kern_task_t *task);
void       kern_watchdog_check(void);
```

## 关键实现

调度器在每次 tick 中调用统计更新、栈溢出检查与看门狗检查。

*📄 Source: [kern_sched.c](../../src/kernel/kern_sched.c#L492-L501)*

```c
sched_check_stack_pressure(g_current_task);
sched_notify_memory_pressure();

kern_stats_update();
if ((g_sched_ticks % 100) == 0) {
    kern_task_stack_overflow_check(g_current_task);
}
if ((g_sched_ticks % 1000) == 0) {
    kern_watchdog_check();
}
```

栈溢出检测通过栈底 canary 值判断。

*📄 Source: [kern_task_stack.c](../../src/kernel/kern_task_stack.c#L159-L173)*

```c
bool kern_task_stack_overflow_check(kern_task_t *task)
{
    ...
    if (canary != KERN_STACK_CANARY) {
        kern_log(KERN_LOG_PANIC, "stack overflow detected ...");
        return true;
    }
    return false;
}
```

## 使用示例

```c
kern_stats_init();
kern_watchdog_register(kern_task_current());

for (;;) {
    do_work();
    kern_watchdog_feed(kern_task_current());
    kern_sleep_ms(10);
}
```

## 注意事项

- 统计更新最小间隔 100ms，避免频繁浮点/除法开销。
- 看门狗超时时间为 `KERN_WATCHDOG_TIMEOUT_MS`（默认 5000ms）。

---

> **See Also:** [任务控制](task-ctrl.md) | [TCB 统计字段](../../src/kernel/kern_task.h#L108-L111)
