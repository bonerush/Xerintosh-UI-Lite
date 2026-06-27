# ISR 安全 IPC

> **Parent:** [Xeros 内核文档](index.md) | **Related:** [IPC 原语](../architecture/ipc-primitives.md)

Xeros 提供一组可在中断服务程序（ISR）中调用的 IPC 原语，用于从硬件中断唤醒任务。

## 核心 API

*📄 Source: [kern_isr.h](../../src/kernel/kern_isr.h#L13-L23)*

```c
kern_err_t kern_bin_sem_give_from_isr(kern_bin_sem_t *sem,
                                       bool *higher_prio_task_woken);
kern_err_t kern_sem_give_from_isr(kern_sem_t *sem,
                                   bool *higher_prio_task_woken);
kern_err_t kern_event_set_from_isr(kern_event_t *ev, uint32_t bits,
                                    bool *higher_prio_task_woken);
kern_err_t kern_task_notify_from_isr(kern_task_t *task, uint32_t value,
                                      kern_notify_action_t action,
                                      bool *higher_prio_task_woken);

void kern_yield_from_isr(void);
```

## 设计要点

- ISR 版本使用相同的自旋锁保护原语状态。
- 若被唤醒任务优先级高于当前运行任务，则设置 `*higher_prio_task_woken = true`，供 ISR 退出前调用 `kern_yield_from_isr`。

*📄 Source: [kern_isr.c](../../src/kernel/kern_isr.c#L5-L13)*

```c
static void isr_note_woken(kern_task_t *woken, bool *higher_prio_task_woken)
{
    if (higher_prio_task_woken == NULL || woken == NULL) return;
    kern_task_t *current = kern_task_current();
    if (current != NULL && woken->priority > current->priority) {
        *higher_prio_task_woken = true;
    }
}
```

## 使用示例

```c
bool woken = false;
kern_sem_give_from_isr(&rx_sem, &woken);
if (woken) {
    kern_yield_from_isr();
}
```

## 注意事项

- ISR 中不能调用阻塞型 take/recv API。
- 事件组 ISR 版本会唤醒所有等待者。

---

> **See Also:** [IPC 原语](../architecture/ipc-primitives.md) | [任务通知](task-notify.md)
