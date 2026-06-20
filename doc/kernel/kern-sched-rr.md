# Round-Robin 调度类（Kern Sched RR）

> **Parent:** [内核总览](index.md) | **Related:** [可插拔调度类](kern-sched-class.md), [优先级 FIFO 类](kern-sched-fifo.md), [调度器](kern-task.md)

## 概述

`sched_class_rr` 是 Xeros 内核的**默认调度类**，实现了经典的 Round-Robin 循环调度策略。它总是第一个被注册（在 `kern_sched_init()` 中），作为所有任务的兜底调度器。

**核心算法**：两遍链表扫描 — 第一遍唤醒到期任务，第二遍从上一轮选中任务的下一个开始循环遍历。

---

## 关键概念

### RR class 实例

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L181-L190)*

```c
kern_sched_class_t sched_class_rr = {
    .name             = "round-robin",
    .enqueue          = sched_rr_enqueue,
    .dequeue          = sched_rr_dequeue,
    .pick_next        = sched_rr_pick_next,
    .tick             = sched_rr_tick,
    .prio_changed     = sched_rr_prio_changed,  /* 空实现，RR 不使用优先级 */
    .memory_pressure  = sched_rr_memory_pressure,
    .task_list        = NULL,  /* 在 kern_sched_init 中设为 g_task_list */
    .task_list_tail   = NULL, /* O(1) 追加用尾指针 */
};
```

### enqueue：O(1) 追加到链表尾部

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L35-L60)*

```c
static void sched_rr_enqueue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    task->next = NULL;
    task->scheduler_class_id = sched_class_rr.class_id;

    if (sched_class_rr.task_list == NULL) {
        sched_class_rr.task_list = task;
        sched_class_rr.task_list_tail = task;
    } else {
        /* 防御：如果 tail 未初始化（例如 kern_sched_init 遗漏），回退 O(n) 尾追加 */
        if (sched_class_rr.task_list_tail != NULL) {
            sched_class_rr.task_list_tail->next = task;
        } else {
            kern_task_t *t = sched_class_rr.task_list;
            while (t->next != NULL) t = t->next;
            t->next = task;
        }
        sched_class_rr.task_list_tail = task;
    }

    task_list_unlock();
}
```

```
函数 RR入队(任务) {
    if (任务为空) 返回

    获取任务链表自旋锁()          // SMP 保护

    任务.next = NULL
    任务.调度类ID = RR类.class_id // 同步任务所属调度类

    if (RR类.task_list 为空) {
        RR类.task_list = 任务
        RR类.task_list_tail = 任务
    } else {
        if (RR类.task_list_tail 不为空) {
            RR类.task_list_tail.next = 任务   // O(1) 尾追加
        } else {
            /* 防御性回退：tail 未初始化时遍历到尾部 */
            当前 = RR类.task_list
            while (当前.next != NULL) 当前 = 当前.next
            当前.next = 任务
        }
        RR类.task_list_tail = 任务
    }

    释放任务链表自旋锁()
}

/* 核心变化：
 * - 使用 task_list_tail 实现 O(1) 追加
 * - SMP 下通过 task_list_lock 保护链表和 tail 指针
 * - 入队时同步 task->scheduler_class_id，标记任务归属
 */
```

### dequeue：从链表中移除

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L64-L97)*

```c
static void sched_rr_dequeue(kern_task_t *task)
{
    if (task == NULL) return;

    task_list_lock();

    kern_task_t **head = &sched_class_rr.task_list;
    kern_task_t *prev = NULL;
    kern_task_t *t = *head;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;
                if (task == sched_class_rr.task_list_tail) {
                    sched_class_rr.task_list_tail = prev;
                }
            } else {
                *head = t->next;
                if (task == sched_class_rr.task_list_tail) {
                    sched_class_rr.task_list_tail = NULL;
                }
            }
            if (g_last_picked == task) g_last_picked = NULL;
            task->scheduler_class_id = -1;
            task_list_unlock();
            return;
        }
        prev = t;
        t = t->next;
    }

    task->scheduler_class_id = -1;
    task_list_unlock();
}
```

```
函数 RR出队(任务) {
    if (任务为空) 返回

    获取任务链表自旋锁()

    头 = &RR类.task_list
    前驱 = NULL
    当前 = *头

    while (当前 != NULL) {
        if (当前 == 要移除的任务) {
            if (前驱 != NULL) {
                前驱.next = 当前.next
                if (任务 == RR类.task_list_tail) {
                    RR类.task_list_tail = 前驱   // 更新尾指针
                }
            } else {
                *头 = 当前.next
                if (任务 == RR类.task_list_tail) {
                    RR类.task_list_tail = NULL   // 链表唯一节点被移除
                }
            }
            if (上一次选中的任务 == 要移除的任务) {
                上一次选中 = NULL                // 清除脏引用
            }
            任务.调度类ID = -1                    // 标记不再属于任何调度类
            释放任务链表自旋锁()
            return
        }
        前驱 = 当前
        当前 = 当前.next
    }

    任务.调度类ID = -1
    释放任务链表自旋锁()
}

/* 核心变化：
 * - 同步更新 task_list_tail，保证 O(1) enqueue 继续正确
 * - 出队时重置 task->scheduler_class_id = -1
 * - 全程在 task_list_lock 保护下操作
 */
```

### pick_next：两遍扫描 Round-Robin

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L101-L152)*

这是 RR 类的核心算法，实现两遍链表扫描 + CPU 亲和性检查。

```c
static kern_task_t *sched_rr_pick_next(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    uint32_t now = g_sched_ticks;

    task_list_lock();

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
                         ? g_last_picked->next
                         : sched_class_rr.task_list;

    if (start == NULL) {
        task_list_unlock();
        return NULL;
    }

    t = start;
    kern_task_t *first_candidate = NULL;
    do {
        if (t->state == KERN_TASK_READY) {
            uint8_t tid = t->cpu_id;
            /* CPU 亲和性检查：KERN_CPU_ANY 或匹配本核心 */
            if (tid == KERN_CPU_ANY || tid == cpu) {
                g_last_picked = t;
                task_list_unlock();
                return t;
            }
            if (first_candidate == NULL) {
                first_candidate = t;
            }
        }
        t = t->next;
        if (t == NULL) {
            t = sched_class_rr.task_list;
        }
    } while (t != start);

    task_list_unlock();

    /* 无亲和性匹配任务时，返回 KERN_CPU_ANY 任务作为兜底 */
    return first_candidate;
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
    首个候选 = NULL
    do {
        if (遍历指针.状态 == 就绪) {
            /* CPU 亲和性检查 */
            if (遍历指针.cpu_id == KERN_CPU_ANY || 遍历指针.cpu_id == 当前CPU) {
                上一次选中 = 遍历指针
                return 遍历指针        /* 找到了！ */
            }
            if (首个候选 == NULL) {
                首个候选 = 遍历指针    /* 记录第一个就绪但不匹配亲和性的任务 */
            }
        }
        遍历指针 = 遍历指针.next
        if (遍历指针 == NULL) {
            遍历指针 = RR类.task_list  /* 回绕到链表头 */
        }
    } while (遍历指针 != 起点)      /* 扫描了完整一圈 */

    return 首个候选  /* 无亲和性匹配时返回候选任务 */
}

/* 核心巧思：
 * 1. 第一遍唤醒保证 sleep 任务到期即可被第二遍选中
 * 2. 从 g_last_picked->next 开始确保公平轮转
 * 3. 回绕机制在单圈找不到任务时回到头部继续
 * 4. CPU 亲和性检查确保任务运行在正确核心上
 * 5. 无亲和性匹配时返回首个候选，避免饿死
 */
```

### tick：时间片递减 + 抢占标记

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L156-L168)*

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

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L172-L177)*

```c
static void sched_rr_prio_changed(kern_task_t *task, uint8_t old_prio)
{
    (void)task;
    (void)old_prio;
    /* RR 不关心优先级，空实现 */
}
```

RR 调度类完全不使用 `priority` 字段。这个回调仅在 FIFO class 中有实际逻辑（见 [kern-sched-fifo.md](kern-sched-fifo.md)）。

### 内存压力回调：高压下缩短时间片

*📄 Source: [kern_sched_rr.c](../../src/kernel/kern_sched_rr.c#L175-L184)*

```c
static void sched_rr_memory_pressure(kern_kmem_pressure_level_t level)
{
    if (level == KERN_KMEM_PRESSURE_HIGH) {
        g_rr_timeslice = SCHED_RR_HIGH_PRESSURE_TIMESLICE;
    } else {
        g_rr_timeslice = SCHED_RR_DEFAULT_TIMESLICE;
    }
}
```

当 `kern_kmem_pressure_level()` 返回 `HIGH` 时，RR 把全局时间片从默认值切换到更短的 `SCHED_RR_HIGH_PRESSURE_TIMESLICE`：

- 低压/中压：使用默认时间片，保证任务吞吐
- 高压：缩短时间片，让内存紧张时各任务更频繁地让出 CPU，降低单任务长时间持有内存峰值的风险

### 默认时间片

*📄 Source: [kern_sched_rr.h](../../src/kernel/kern_sched_rr.h#L22-L23)*

```c
#define SCHED_RR_DEFAULT_TIMESLICE 10
#define SCHED_RR_HIGH_PRESSURE_TIMESLICE 3
```

| 参数 | 值 | 说明 |
|------|-----|------|
| 默认时间片 | 10 tick | 每个任务连续运行 10 个 tick 后被强制切换 |
| 高压时间片 | 3 tick | 内存压力 HIGH 时缩短切换间隔 |
| tick 频率 | ~1000 Hz | 约 1ms 一个 tick |
| 时间片时长 | ~10ms / ~3ms | 默认/高压下一轮 Round-Robin 的时长 |

---

## 与其他组件的关系

- **kern_sched_class**：`sched_class_rr` 是 `kern_sched_class_t` 接口的实例
- **kern_sched**：`kern_sched_init()` 首先注册此 class，并设置 `sched_class_rr.task_list = g_task_list`（Native / ESP32 均如此）
- **kern_task**：RR 作为默认调度类，所有任务默认由其调度。`sched_class_rr.task_list` 直接指向全局 `g_task_list`
- **g_last_picked**：跨 tick 的状态变量，记录上一轮选中的任务，实现真正的轮转

---

> **See Also:** [可插拔调度类](kern-sched-class.md) | [优先级 FIFO 类](kern-sched-fifo.md) | [调度器](kern-task.md)
