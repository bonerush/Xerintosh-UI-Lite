# 优先级 FIFO 调度类（Kern Sched FIFO）

> **Parent:** [内核总览](index.md) | **Related:** [可插拔调度类](kern-sched-class.md), [Round-Robin 类](kern-sched-rr.md), [调度器](kern-task.md)

## 概述

`sched_class_fifo` 是 Xeros 内核的**抢占式优先级调度类**。仅在 `CONFIG_PREEMPT_ENABLED` 编译宏定义时注册并激活。任务按优先级（0-255）在链表中排序，`pick_next()` 始终选择最高优先级的就绪任务。当更高优先级任务变为就绪时，立即设置 `g_need_resched` 触发抢占。

**核心算法**：优先级有序链表（高→低）+ 入队时抢占检测 + tick 中扫描更高优先级任务。

---

## 关键概念

### FIFO class 实例

*📄 Source: [kern_sched_fifo.c](../../src/kernel/kern_sched_fifo.c#L128-L136)*

```c
kern_sched_class_t sched_class_fifo = {
    .name          = "priority-fifo",
    .enqueue       = sched_fifo_enqueue,
    .dequeue       = sched_fifo_dequeue,
    .pick_next     = sched_fifo_pick_next,
    .tick          = sched_fifo_tick,
    .prio_changed  = sched_fifo_prio_changed,
    .task_list     = NULL,
};
```

FIFO class 拥有独立的 `task_list`，与 RR class 的 `task_list` 分离。同一任务只会在一个 class 的链表中。

### enqueue：按优先级有序插入 + 抢占检测

*📄 Source: [kern_sched_fifo.c](../../src/kernel/kern_sched_fifo.c#L19-L47)*

```c
static void sched_fifo_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    kern_task_t **head = &sched_class_fifo.task_list;
    task->next = NULL;

    /* 空链表或插入到头部（最高优先级在最前） */
    if (*head == NULL || (*head)->priority <= task->priority) {
        task->next = *head;
        *head = task;
    } else {
        /* 找到正确的插入位置（维持降序：高→低） */
        kern_task_t *t = *head;
        while (t->next != NULL && t->next->priority > task->priority) {
            t = t->next;
        }
        task->next = t->next;
        t->next = task;
    }

#ifdef CONFIG_PREEMPT_ENABLED
    /* 如果入队任务优先级高于当前运行的任务，触发抢占 */
    if (g_current_task != NULL
        && task->state == KERN_TASK_READY
        && task->priority > g_current_task->priority) {
        g_need_resched = true;
    }
#endif
}
```

#### 中文伪代码拆解

```
函数 FIFO入队(任务) {
    if (任务 == NULL) 返回

    头 = &FIFO类.task_list
    任务.next = NULL

    /* 步骤1：找到正确的插入位置（链表按优先级降序排列） */
    if (链表为空 或 链表头的优先级 <= 新任务的优先级) {
        /* 新任务优先级最高 → 插入到头部 */
        任务.next = *头
        *头 = 任务
    } else {
        /* 在链表中搜索第一个优先级 <= 新任务的位置 */
        当前 = *头
        while (当前.next 存在 且 当前.next.优先级 > 新任务.优先级) {
            当前 = 当前.next  /* 跳过更高优先级的任务 */
        }
        任务.next = 当前.next
        当前.next = 任务     /* 插入在此位置之后 */
    }

    /* ___步骤2：抢占检测___ */
    [仅抢占模式]
    if (当前运行的任务 != NULL
        且 新任务.状态 == 就绪
        且 新任务.优先级 > 当前运行任务.优先级) {
        需重调度 = true  /* → 下一个 tick 将立即切换到更高优先级任务 */
    }
}

/* 链表排序示例：
 * 插入优先级顺序: idle(0), wifi(50), shell(100), ui(200)
 *
 * 链表最终状态（头→尾）:
 *   ui(200) → shell(100) → wifi(50) → idle(0) → NULL
 *
 * 插入 ui(200) 时:
 *   - 链表为空 → 直接作为头
 *
 * 插入 shell(100) 时:
 *   - 从头开始遍历
 *   - 头(200) > shell(100) → 继续
 *   - 头.next == NULL → 插入在 ui 之后
 *
 * 插入 wifi(50) 时:
 *   - 从头开始遍历
 *   - ui(200) > wifi(50) → 继续
 *   - shell(100) > wifi(50) → 继续
 *   - shell.next == NULL → 插入在 shell 之后
 */
```

### dequeue：标准链表移除

*📄 Source: [kern_sched_fifo.c](../../src/kernel/kern_sched_fifo.c#L51-L70)*

```c
static void sched_fifo_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

    kern_task_t **head = &sched_class_fifo.task_list;
    kern_task_t *prev = NULL;
    kern_task_t *t = *head;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;
            } else {
                *head = t->next;
            }
            return;
        }
        prev = t;
        t = t->next;
    }
}
```

逻辑与 RR 的 `dequeue` 相同，不赘述。

### pick_next：选择最高优先级就绪任务

*📄 Source: [kern_sched_fifo.c](../../src/kernel/kern_sched_fifo.c#L74-L88)*

```c
static kern_task_t *sched_fifo_pick_next(void)
{
    kern_task_t *t = sched_class_fifo.task_list;
    while (t != NULL) {
        /* 唤醒到期 sleep 任务 */
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= g_sched_ticks) {
            t->state = KERN_TASK_READY;
        }
        if (t->state == KERN_TASK_READY) {
            return t;  /* 链表按优先级排序，第一个就绪即最高优先级 */
        }
        t = t->next;
    }
    return NULL;
}
```

#### 中文伪代码拆解

```
函数 FIFO选择下一个任务() {
    当前时间 = g_sched_ticks
    遍历指针 = FIFO类.task_list

    while (遍历指针 != NULL) {
        /* 边遍历边唤醒到期任务 */
        if (遍历指针.状态 == 睡眠 且 遍历指针.唤醒时间 <= 当前时间) {
            遍历指针.状态 = 就绪
        }

        if (遍历指针.状态 == 就绪) {
            return 遍历指针  /* 第一个就绪 = 最高优先级就绪任务 */
        }

        遍历指针 = 遍历指针.next
    }

    return NULL
}

/* 关键洞察：
 * 因为链表按优先级降序排列，
 * 第一个状态为 READY 的任务就是当前最高优先级的就绪任务。
 * 不需要遍历整个链表比较优先级 ← O(n) 找到即停
 *
 * 与 RR 的区别：
 * - RR 需要从 g_last_picked->next 开始循环查找
 * - FIFO 总是从头开始，头就是最高优先级
 */
```

### tick：定期检查更高优先级任务

*📄 Source: [kern_sched_fifo.c](../../src/kernel/kern_sched_fifo.c#L92-L113)*

```c
static void sched_fifo_tick(kern_task_t *current)
{
#ifdef CONFIG_PREEMPT_ENABLED
    if (current == NULL) return;

    /* 检查 FIFO class 中是否有比当前任务优先级更高的就绪任务 */
    kern_task_t *t = sched_class_fifo.task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_READY && t->priority > current->priority) {
            g_need_resched = true;
            return;
        }
        /* 链表按优先级排序，第一个不更优则后面都不会更优 */
        if (t->priority <= current->priority) {
            break;
        }
        t = t->next;
    }
#else
    (void)current;
#endif
}
```

#### 中文伪代码拆解

```
函数 FIFO_tick(当前任务) {
    [仅在抢占模式]

    if (当前任务 == NULL) 返回

    遍历指针 = FIFO类.task_list
    while (遍历指针 != NULL) {
        if (遍历指针.状态 == 就绪 且 遍历指针.优先级 > 当前任务.优先级) {
            需重调度 = true   /* 有更高优先级任务就绪，立即抢占 */
            return
        }
        /* 优化：链表有序，第一个优先级不更高 → 后面都不会更高 */
        if (遍历指针.优先级 <= 当前任务.优先级) {
            break  /* 提前退出，省遍历 */
        }
        遍历指针 = 遍历指针.next
    }

    [非抢占模式] 空函数
}

/* 抢占触发条件：
 * 1. 当前任务时间片用尽 → RR_tick 设置 g_need_resched
 * 2. 更高优先级任务入队 → enqueue 设置 g_need_resched
 * 3. 更高优先级任务被唤醒 → tick 检测到并设置 g_need_resched
 *
 * 注意优先级抢占与 RR 时间片的协作：
 * - 如果 FIFO 标记了抢占，调度器立即切换
 * - 如果没有更高优先级任务，RR 的时间片机制保证公平
 */
```

### prio_changed：动态优先级变更时重新排序

*📄 Source: [kern_sched_fifo.c](../../src/kernel/kern_sched_fifo.c#L117-L124)*

```c
static void sched_fifo_prio_changed(kern_task_t *task, uint8_t old_prio)
{
    if (task == NULL) return;
    /* 移除再按新优先级重新插入 */
    sched_fifo_dequeue(task);
    sched_fifo_enqueue(task);
    (void)old_prio;
}
```

#### 中文伪代码拆解

```
函数 FIFO优先级变更(任务, 旧优先级) {
    if (任务 == NULL) 返回

    /* 两步走：先移除，再按新优先级重新插入 */
    FIFO出队(任务)       /* 从有序链表中移除 */
    FIFO入队(任务)       /* 按新的 priority 值重新插入到正确位置 */

    /* 注意：enqueue 内部会检测是否需要抢占 */
}
```

#### 优先级变更的完整流程

```
初始链表: taskA(200) → taskB(100) → taskC(50)

taskC.priority 从 50 提升到 150:

1. dequeue(taskC)
   链表: taskA(200) → taskB(100) → NULL

2. enqueue(taskC)  // priority=150
   遍历: taskA(200) > taskC(150) → 继续
         taskB(100) <= taskC(150) → 插入在 taskA 和 taskB 之间
   链表: taskA(200) → taskC(150) → taskB(100) → NULL

3. 抢占检测:
   taskC.priority(150) > g_current_task.priority?
   → 如果是，标记 g_need_resched
```

---

## 与 RR 类的协作

```
调度类优先级链:

   pick_next_ready() 按注册顺序查询:
   ┌─────────────────────────────────────┐
   │ g_sched_classes[0] = sched_class_rr │  ← 兜底（先注册）
   │ g_sched_classes[1] = sched_class_fifo│  ← 高优先级（后注册）
   └─────────────────────────────────────┘

   查询流程:
   1. FIFO.pick_next() → 如果返回任务 → 直接使用
   2. 如果 FIFO 返回 NULL → RR.pick_next() → 兜底

   结果：
   - 有高优先级就绪任务 → 总是选择它
   - 高优先级任务全部睡眠/阻塞 → RR 循环调度低优先级任务
   - idle(priority=0) 作为最终兜底
```

---

## 与其他组件的关系

- **kern_sched_class**：`sched_class_fifo` 是 `kern_sched_class_t` 接口的实例
- **kern_sched**：`kern_sched_init()` 中仅在 `CONFIG_PREEMPT_ENABLED` 时注册此类
- **kern_task**：每个任务通过 `scheduler_class_id = 1` 指向此类。任务的 `priority` 字段决定链表中的位置
- **kern_sched_rr**：与 RR class 协作完成两级调度（FIFO 优先，RR 兜底）

---

> **See Also:** [可插拔调度类](kern-sched-class.md) | [Round-Robin 类](kern-sched-rr.md) | [调度器](kern-task.md)
