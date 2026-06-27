# 任务通知

> **Parent:** [Xeros 内核文档](index.md) | **Related:** [任务控制](task-ctrl.md) | [IPC 原语](../architecture/ipc-primitives.md)

任务通知是 Xeros 提供的轻量级任务间同步机制。每个 TCB 自带一个 32 位通知值与等待状态，相比信号量/队列减少了动态分配开销。

## 核心 API

*📄 Source: [kern_task_notify.h](../../src/kernel/kern_task_notify.h#L20-L34)*

```c
kern_err_t kern_task_notify_init(void);
kern_err_t kern_task_notify_give(kern_task_t *task);
kern_err_t kern_task_notify(kern_task_t *task, uint32_t value,
                            kern_notify_action_t action);
bool       kern_task_notify_take(bool clear_on_exit, uint32_t timeout_ms);
uint32_t   kern_task_notify_wait_bits(uint32_t bits_to_wait, bool clear_on_exit,
                                       bool wait_all, uint32_t timeout_ms);
```

动作类型：

*📄 Source: [kern_task_notify.h](../../src/kernel/kern_task_notify.h#L20-L26)*

```c
typedef enum {
    KERN_NOTIFY_NO_ACTION = 0,
    KERN_NOTIFY_SET_BITS,
    KERN_NOTIFY_INCREMENT,
    KERN_NOTIFY_SET_VALUE_WITH_OVERWRITE,
    KERN_NOTIFY_SET_VALUE_WITHOUT_OVERWRITE
} kern_notify_action_t;
```

## 关键实现

发送方在持有自旋锁时修改 `notify_value`，并调用 `notify_wake_locked` 唤醒处于 `SLEEPING` 的接收任务。

*📄 Source: [kern_task_notify.c](../../src/kernel/kern_task_notify.c#L28-L37)*

```c
static void notify_wake_locked(kern_task_t *task)
{
    task->notify_state = KERN_NOTIFY_RECEIVED;
    if (task->state == KERN_TASK_SLEEPING) {
        task->state = KERN_TASK_READY;
#ifdef CONFIG_SMP_ENABLED
        kern_smp_ipi_reschedule(task->cpu_id);
#endif
    }
}
```

等待侧使用短睡眠重试循环，避免 `unlock -> sleep` 窗口丢失唤醒。

*📄 Source: [kern_task_notify.c](../../src/kernel/kern_task_notify.c#L88-L143)*

```c
bool kern_task_notify_take(bool clear_on_exit, uint32_t timeout_ms)
{ ... }
```

## 使用示例

```c
kern_task_notify_init();

/* 任务 A：设置位通知任务 B */
kern_task_notify(task_b, 0x01, KERN_NOTIFY_SET_BITS);

/* 任务 B：等待位 */
uint32_t bits = kern_task_notify_wait_bits(0x01, true, false, 1000);
```

## 注意事项

- `kern_task_notify_take(false, ...)` 将 `notify_value` 减 1；`clear_on_exit=true` 时清零。
- `KERN_NOTIFY_SET_VALUE_WITHOUT_OVERWRITE` 仅在接收方处于 `KERN_NOTIFY_WAITING` 时才写入，用于一次性同步。

---

> **See Also:** [任务控制](task-ctrl.md) | [TCB 字段说明](../../src/kernel/kern_task.h#L104-L111)
