# 任务控制

> **Parent:** [Xeros 内核文档](index.md) | **Related:** [任务通知](task-notify.md)

任务控制接口提供挂起、恢复、优先级设置与延迟等 FreeRTOS 等价功能。

## 核心 API

*📄 Source: [kern_task_ctrl.h](../../src/kernel/kern_task_ctrl.h#L11-L18)*

```c
kern_err_t kern_task_suspend(kern_task_t *task);
kern_err_t kern_task_resume(kern_task_t *task);

uint8_t    kern_task_priority_get(const kern_task_t *task);
kern_err_t kern_task_priority_set(kern_task_t *task, uint8_t new_prio);

void       kern_task_delay(uint32_t ms);
void       kern_task_delay_until(uint32_t *prev_wake_time, uint32_t ms);
```

## 关键实现

`kern_task_priority_set` 同时更新 `priority` 与 `base_priority`，确保 PI 互斥锁恢复时使用新的基线优先级。

*📄 Source: [kern_task_ctrl.c](../../src/kernel/kern_task_ctrl.c#L36-L58)*

```c
kern_err_t kern_task_priority_set(kern_task_t *task, uint8_t new_prio)
{
    ...
    task->priority = new_prio;
    task->base_priority = new_prio;
    ...
}
```

`kern_task_delay_until` 基于绝对时间点计算剩余时间，适合周期性任务。

*📄 Source: [kern_task_ctrl.c](../../src/kernel/kern_task_ctrl.c#L65-L77)*

```c
void kern_task_delay_until(uint32_t *prev_wake_time, uint32_t ms)
{ ... }
```

## 使用示例

```c
kern_task_priority_set(NULL, 10);  /* 提升当前任务优先级 */
kern_task_delay(100);              /* 相对延迟 100ms */

uint32_t prev = g_sched_ticks;
for (;;) {
    do_work();
    kern_task_delay_until(&prev, 50);
}
```

## 注意事项

- 挂起当前任务会立即 `kern_yield()`。
- `kern_task_resume` 仅对 `SUSPENDED` 状态任务有效。

---

> **See Also:** [任务通知](task-notify.md) | [TCB 定义](../../src/kernel/kern_task.h#L56-L115)
