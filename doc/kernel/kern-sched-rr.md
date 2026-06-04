# Round-Robin 调度类（Kern Sched RR）

> **Parent:** [内核总览](index.md) | **Related:** [可插拔调度类](kern-sched-class.md), [优先级 FIFO 类](kern-sched-fifo.md), [调度器](kern-task.md)

## 概述

`sched_class_rr` 是 Xeros 内核的**默认调度类**，实现了经典的 Round-Robin 循环调度策略。它总是第一个被注册（在 `kern_sched_init()` 中），作为所有任务的兜底调度器。

**核心算法**：两遍链表扫描 — 第一遍唤醒到期任务，第二遍从上一轮选中任务的下一个开始循环遍历。

---

## 关键概念

### RR class 实例

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L136-L144)*

```c
kern_sched_class_t sched_class_rr = {
    .name          = "round-robin",
    .enqueue       = sched_rr_enqueue,
    .dequeue       = sched_rr_dequeue,
    .pick_next     = sched_rr_pick_next,
    .tick          = sched_rr_tick,
    .prio_changed  = sched_rr_prio_changed,  /* 空实现，RR 不使用优先级 */
    .task_list     = NULL,  /* 在 kern_sched_init 中设为 g_task_list */
};
```

### enqueue：追加到链表尾部

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L24-L39)*

```c
static void sched_rr_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    kern_task_t **head = &sched_class_rr.task_list;
    kern_task_t *t = *head;
    if (t == NULL) {
        *head = task;
        task->next = NULL;
        return;
    }
    while (t->next != NULL) t = t->next;
    t->next = task;
    task->next = NULL;
}
```

```
函数 RR入队(任务) {
    if (任务为空) 返回

    头指针 = &RR类.task_list
    当前 = *头指针

    if (链表为空) {
        头 = 任务
        任务.next = NULL
        return
    }

    /* 遍历到链表尾部 */
    循环 { if (当前.next == NULL) 跳出; 当前 = 当前.next }
    当前.next = 任务
    任务.next = NULL
}

/* 追加到尾部意味着：
 * - 新创建的任务排在所有已有任务之后
 * - Round-Robin 轮转中，新任务获得公平的初始位置
 */
```

### dequeue：从链表中移除

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L43-L63)*

```c
static void sched_rr_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

    kern_task_t **head = &sched_class_rr.task_list;
    kern_task_t *prev = NULL;
    kern_task_t *t = *head;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;    /* 中间节点：跳过 */
            } else {
                *head = t->next;          /* 头节点：更新头指针 */
            }
            if (g_last_picked == task) g_last_picked = NULL;  /* 清除选中标记 */
            return;
        }
        prev = t;
        t = t->next;
    }
}
```

```
函数 RR出队(任务) {
    if (任务为空) 返回

    头 = &RR类.task_list
    前驱 = NULL
    当前 = *头

    while (当前 != NULL) {
        if (当前 == 要移除的任务) {
            if (前驱 != NULL) {
                前驱.next = 当前.next   /* 跳过当前节点 */
            } else {
                *头 = 当前.next        /* 当前是头节点 */
            }
            if (上一次选中的任务 == 要移除的任务) {
                上一次选中 = NULL       /* 清除脏引用 */
            }
            return
        }
        前驱 = 当前
        当前 = 当前.next
    }
}

/* 注意 g_last_picked 的处理：
 * 如果被移除的任务恰好是 g_last_picked，
 * pick_next 将从链表头重新开始扫描，防止野指针
 */
```

### pick_next：两遍扫描 Round-Robin

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L67-L103)*

这是 RR 类的核心算法，实现两遍链表扫描。

```c
static kern_task_t *sched_rr_pick_next(void)
{
    uint32_t now = g_sched_ticks;

    /* ═══ 第一遍：唤醒所有到期 sleep 任务 ═══ */
    kern_task_t *t = sched_class_rr.task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* ═══ 第二遍：Round-Robin 扫描 ═══ */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next       /* 从上一个的下一个开始 */
                         : sched_class_rr.task_list;  /* 否则从头开始 */

    if (start == NULL) return NULL;  /* 任务列表为空 */

    t = start;
    do {
        if (t->state == KERN_TASK_READY) {
            g_last_picked = t;      /* 记录本轮选中 */
            return t;
        }
        t = t->next;
        if (t == NULL) {
            t = sched_class_rr.task_list;  /* 到达尾部 → 回到头部 */
        }
    } while (t != start);  /* 扫描一圈回到起点 = 没有就绪任务 */

    return NULL;
}
```

#### 中文伪代码拆解

```
函数 RR选择下一个任务() {
    当前时间 = g_sched_ticks

    /* ──── 第一遍扫描：睡眠唤醒 ──── */
    遍历指针 = RR类.task_list
    while (遍历指针 != NULL) {
        if (遍历指针.状态 == 睡眠 且 遍历指针.唤醒时间 <= 当前时间) {
            遍历指针.状态 = 就绪   /* 唤醒！ */
        }
        遍历指针 = 遍历指针.next
    }

    /* ──── 确定起点 ──── */
    if (上一次选中的任务不为NULL 且 它后面还有任务) {
        起点 = 上一次选中的.next   /* 从上次之后继续轮转 */
    } else {
        起点 = RR类.task_list      /* 回到链表头 */
    }

    if (起点 == NULL) 返回 NULL    /* 空链表 */

    /* ──── 第二遍扫描：循环查找就绪任务 ──── */
    遍历指针 = 起点
    do {
        if (遍历指针.状态 == 就绪) {
            上一次选中 = 遍历指针
            return 遍历指针        /* 找到了！ */
        }
        遍历指针 = 遍历指针.next
        if (遍历指针 == NULL) {
            遍历指针 = RR类.task_list  /* 回绕到链表头 */
        }
    } while (遍历指针 != 起点)      /* 扫描了完整一圈 */

    return NULL  /* 所有任务都不可运行（全都睡眠/阻塞/僵尸） */
}

/* 核心巧思：
 * 1. 第一遍唤醒保证 sleep 任务到期即可被第二遍选中
 * 2. 从 g_last_picked->next 开始确保公平轮转
 * 3. 回绕机制在单圈找不到任务时回到头部继续
 * 4. 全部不可运行返回 NULL → idle 任务兜底
 */
```

#### 两遍扫描的时序图

```
假设链表: idle → shell → wifi → ui

上一次选中 = shell

第一次 tick:
  第一遍: 检查所有 sleep 任务 → wifi 到期 → wifi.state = READY
  第二遍: 从 shell.next = wifi 开始
          → wifi.state == READY → 返回 wifi
          → g_last_picked = wifi

第二次 tick:
  第一遍: 检查 sleep → 无到期
  第二遍: 从 wifi.next = ui 开始
          → ui.state == SLEEPING → 跳过
          → ui.next = NULL → 回到 idle
          → idle.state == READY → 返回 idle
          → g_last_picked = idle

第三次 tick:
  第一遍: 检查 sleep → ui 到期 → ui.state = READY
  第二遍: 从 idle.next = shell 开始
          → shell.state == SLEEPING → 跳过
          → shell.next = wifi → wifi == READY → 返回 wifi
```

### tick：时间片递减 + 抢占标记

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L107-L123)*

```c
static void sched_rr_tick(kern_task_t *current)
{
    if (current == NULL) return;

    if (current->timeslice_remaining > 0) {
        current->timeslice_remaining--;
    }
    if (current->timeslice_remaining == 0) {
        /* 时间片用尽，触发重调度 */
        current->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
        g_need_resched = true;
    }
}
```

#### 中文伪代码拆解

```
函数 RR_tick(当前任务) {
    [仅在抢占模式]
    if (当前任务 == NULL) 返回

    if (时间片剩余 > 0) {
        时间片剩余--  /* 消耗一个 tick */
    }

    if (时间片剩余 == 0) {
        时间片剩余 = 默认时间片(10)   /* 重新装满 */
        g_need_resched = true         /* 标记抢占 → 下一个 tick 会切换 */
    }

    [非抢占模式] 空函数，无操作
}

/* 时间片机制的行为：
 * - 每个任务初始获得 10 个 tick
 * - 每个 tick 消耗 1 个
 * - 10 个 tick 后自动标记抢占
 * - 如果任务是唯一就绪的，即使标记了抢占也不会真正切换
 *   （pick_next_ready 会再次选中同一个任务）
 */
```

### prio_changed：RR 的空实现

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L127-L132)*

```c
static void sched_rr_prio_changed(kern_task_t *task, uint8_t old_prio)
{
    (void)task;
    (void)old_prio;
    /* RR 不关心优先级，空实现 */
}
```

RR 调度类完全不使用 `priority` 字段。这个回调仅在 FIFO class 中有实际逻辑（见 [kern-sched-fifo.md](kern-sched-fifo.md)）。

### 默认时间片

*📄 Source: [kern_sched_rr.h](../../src/kernel/kern_sched_rr.h#L22)*

```c
#define SCHED_RR_DEFAULT_TIMESLICE 10
```

| 参数 | 值 | 说明 |
|------|-----|------|
| 默认时间片 | 10 tick | 每个任务连续运行 10 个 tick 后被强制切换 |
| tick 频率 | ~1000 Hz | 约 1ms 一个 tick |
| 时间片时长 | ~10ms | 在 10ms 内完成一轮 Round-Robin |

---

## 与其他组件的关系

- **kern_sched_class**：`sched_class_rr` 是 `kern_sched_class_t` 接口的实例
- **kern_sched**：`kern_sched_init()` 首先注册此 class，并设置 `sched_class_rr.task_list = g_task_list`
- **kern_task**：每个任务通过 `scheduler_class_id = 0` 指向此 class
- **g_last_picked**：跨 tick 的状态变量，记录上一轮选中的任务，实现真正的轮转

---

> **See Also:** [可插拔调度类](kern-sched-class.md) | [优先级 FIFO 类](kern-sched-fifo.md) | [调度器](kern-task.md)
