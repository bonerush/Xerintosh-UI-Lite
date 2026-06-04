# 可插拔调度器类框架（Kern Sched Class）

> **Parent:** [内核总览](index.md) | **Related:** [Round-Robin 类](kern-sched-rr.md), [优先级 FIFO 类](kern-sched-fifo.md), [调度器](kern-task.md)

## 概述

`kern_sched_class.h/c` 定义了 Xeros 内核的**可插拔调度器类框架**。这是一个面向接口的设计，允许在编译时注册多种调度策略（Round-Robin、优先级 FIFO 等），调度器在每次 tick 中按优先级顺序遍历各 class 选择任务。

**核心思想**：将调度策略从调度器核心逻辑中解耦。每个调度策略实现 `kern_sched_class_t` 接口，通过 `kern_sched_class_register()` 注册到全局 class 数组。`pick_next_ready()` 按注册顺序依次查询各 class。

---

## 关键概念

### 调度器类接口（kern_sched_class_t）

*📄 Source: [kern_sched_class.h](../../src/kernel/kern_sched_class.h#L30-L41)*

```c
typedef struct kern_sched_class {
    const char *name;                                  /* 类名称（调试用） */

    void (*enqueue)(struct kern_task *task);           /* 将任务加入本 class */
    void (*dequeue)(struct kern_task *task);           /* 将任务从本 class 移除 */
    struct kern_task *(*pick_next)(void);              /* 从本 class 中选择下一个任务 */
    void (*tick)(struct kern_task *current);           /* 定时器 tick：时间片递减/抢占检测 */
    void (*prio_changed)(struct kern_task *task,
                         uint8_t old_prio);            /* 任务优先级变更通知 */

    struct kern_task *task_list;                       /* 本 class 的任务链表头 */
} kern_sched_class_t;
```

#### 五个回调函数的职责

| 回调 | 调用时机 | 职责 |
|------|----------|------|
| `enqueue(task)` | `kern_spawn()` 创建任务后 | 将任务加入本 class 的调度链表（RR 追加尾部，FIFO 按优先级插入） |
| `dequeue(task)` | 任务退出 / class 切换时 | 从本 class 的调度链表中移除任务 |
| `pick_next()` | `pick_next_ready()` 遍历时 | 返回本 class 中应运行的下一个任务（NULL = 无就绪任务） |
| `tick(current)` | 每个调度 tick | 时间片递减、抢占标记、睡眠唤醒等周期性操作 |
| `prio_changed(task, old)` | 任务优先级被修改时 | 重组链表顺序（FIFO 需要按新优先级重排，RR 忽略） |

### 全局 class 注册表

*📄 Source: [kern_sched_class.h](../../src/kernel/kern_sched_class.h#L45-L51)*

```c
/** 全局调度器 class 数组（按优先级顺序，NULL 终止） */
extern kern_sched_class_t *g_sched_classes[];

/** 当前注册的 class 数量 */
extern uint8_t g_sched_class_count;

/** 最大可注册的 class 数量 */
#define KERN_SCHED_MAX_CLASSES 8
```

#### 中文伪代码拆解

```
/* 全局 class 注册表结构：

   g_sched_classes[8] = { 指针, 指针, NULL, NULL, ... }
                            │      │
                            ▼      ▼
                         sched_class_rr   sched_class_fifo
                         (第 0 个注册)     (第 1 个注册)

   g_sched_class_count = 2

   优先级规则：先注册的 class 先被 pick_next_ready 查询。
   这意味着：
   - RR 先注册 → RR 作为兜底调度类
   - FIFO 后注册 → 但 FIFO 的 pick_next 返回高优先级任务
                   如果 FIFO 返回了任务，RR 不会被查询
                   如果 FIFO 返回 NULL，RR 提供兜底
*/
```

### 注册流程

*📄 Source: [kern_sched_class.c](../../src/kernel/kern_sched_class.c#L14-L21)*

```c
kern_sched_class_t *g_sched_classes[KERN_SCHED_MAX_CLASSES] = { NULL };
uint8_t g_sched_class_count = 0;

void kern_sched_class_register(kern_sched_class_t *cls)
{
    if (cls == NULL || g_sched_class_count >= KERN_SCHED_MAX_CLASSES) return;
    g_sched_classes[g_sched_class_count++] = cls;
}
```

```
函数 注册调度类(类指针) {
    if (指针为空 或 已满8个) 返回
    类数组[当前计数] = 类指针
    当前计数++
}

/* 注册顺序决定优先级：
 * 1. kern_sched_init() 中先 register(&sched_class_rr)
 * 2. 再 register(&sched_class_fifo)
 *
 * pick_next_ready() 会按顺序查询：
 *   g_sched_classes[0] → RR class → 如果没有就绪任务，返回 NULL
 *   g_sched_classes[1] → FIFO class → 返回最高优先级任务（如果有）
 */
```

### pick_next_ready：核心选择逻辑

*📄 Source: [kern_sched_class.c](../../src/kernel/kern_sched_class.c#L23-L32)*

```c
struct kern_task *pick_next_ready(void)
{
    for (uint8_t i = 0; i < g_sched_class_count; i++) {
        kern_sched_class_t *cls = g_sched_classes[i];
        if (cls == NULL || cls->pick_next == NULL) continue;
        struct kern_task *task = cls->pick_next();
        if (task != NULL) return task;
    }
    return NULL;
}
```

#### 中文伪代码拆解

```
函数 选择下一个就绪任务() {
    /* 按注册顺序依次查询各调度类的 pick_next */
    遍历 i = 0 到 (已注册类数量-1):
        当前类 = g_sched_classes[i]

        if (当前类为空 或 当前类没有pick_next函数) 跳过

        候选任务 = 当前类->pick_next()
        if (候选任务 != NULL) {
            立即返回 候选任务
            /* 找到第一个就绪任务后停止，后面的类不会被查询 */
        }

    返回 NULL  /* 所有类都没有就绪任务 */
}

/* 关键设计：
 * - 先注册的类优先查询（RR 先注册 → 作为兜底）
 * - 返回第一个非 NULL 结果（短路求值）
 * - 每个类的 pick_next 负责自己的选择逻辑
 *   RR：两遍扫描，Round-Robin 轮转
 *   FIFO：按优先级链表顺序返回就绪任务
 */
```

### 调度 tick 中的 class 遍历

*📄 Source: [kern_sched.c](../../src/kernel/kern_sched.c#L206-L211)*

```c
#ifdef CONFIG_PREEMPT_ENABLED
    /* 遍历所有 class 的 tick 回调 */
    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }
    /* 如果任何 class 设置了 g_need_resched → 立即上下文切换 */
    if (g_need_resched) {
        /* ... 抢占调度 ... */
    }
#endif
```

#### 完整调度流程图

```
       ┌───────────────────────────────────────────────────────┐
       │              kern_sched_tick() 入口                    │
       └───────────────────────┬───────────────────────────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │  g_sched_ticks++    │  (per-CPU)
                    │  reap_zombies()     │
                    └─────────┬───────────┘
                              │
                    ┌─────────▼───────────┐
                    │ CONFIG_PREEMPT?     │
                    └──────┬──────┬───────┘
                           │ 是   │ 否
                           ▼      ▼
                ┌──────────────┐ ┌──────────────┐
                │ 遍历所有class │ │ 简单调度模式  │
                │ ->tick() 回调 │ │              │
                └──────┬───────┘ └──────┬───────┘
                       │                │
                       ▼                ▼
               ┌──────────────┐ ┌──────────────┐
               │need_resched? │ │pick_next_ready│
               │  或状态变更? │ │     ()       │
               └──┬────────┬──┘ └──────┬───────┘
                  │ 是     │ 否         │
                  ▼        ▼            ▼
            ┌────────┐ ┌──────┐   ┌──────────┐
            │抢占调度 │ │继续  │   │上下文切换 │
            │立即切换 │ │运行  │   │(swapcontext
            └────────┘ └──────┘   │ /setjmp/  │
                                  │ mutex)    │
                                  └──────────┘
```

### class 注册与任务分配

```
初始化阶段 (kern_sched_init):

    注册 sched_class_rr (index=0)
        │
        ├─ [CONFIG_PREEMPT_ENABLED]
        │   注册 sched_class_fifo (index=1)
        │
        └─ 创建 idle 任务
           idle->scheduler_class_id = 0  (指向 RR class)

任务创建阶段 (kern_spawn):

    分配 TCB
    task->scheduler_class_id = -1   (待分配)
    │
    ├─ [CONFIG_PREEMPT_ENABLED]
    │   task->scheduler_class_id = 1  (指向 FIFO class)
    │   调用 sched_class_fifo.enqueue(task)  (按优先级插入)
    │
    └─ [纯协作模式]
        task->scheduler_class_id = 0  (指向 RR class)
        调用 sched_class_rr.enqueue(task)  (追加到尾部)
```

---

## 扩展：如何添加新的调度类

```
添加新调度类的步骤：

1. 创建 kern_sched_xxx.h:
   extern kern_sched_class_t sched_class_xxx;

2. 创建 kern_sched_xxx.c:
   static kern_task_t *xxx_pick_next(void) { ... }
   static void xxx_enqueue(kern_task_t *task) { ... }
   static void xxx_dequeue(kern_task_t *task) { ... }
   static void xxx_tick(kern_task_t *current) { ... }
   static void xxx_prio_changed(kern_task_t *task, uint8_t old) { ... }

   kern_sched_class_t sched_class_xxx = {
       .name         = "my-scheduler",
       .enqueue      = xxx_enqueue,
       .dequeue      = xxx_dequeue,
       .pick_next    = xxx_pick_next,
       .tick         = xxx_tick,
       .prio_changed = xxx_prio_changed,
       .task_list    = NULL,
   };

3. 在 kern_sched_init() 中注册:
   kern_sched_class_register(&sched_class_xxx);

4. 确保注册顺序符合你的意图（先注册的优先查询）
```

---

## 与其他组件的关系

- **kern_sched**：调度器主循环调用 `pick_next_ready()` 和各 class 的 `tick()` 回调
- **kern_task**：任务的 `scheduler_class_id` 指向所属 class 在 `g_sched_classes[]` 中的索引
- **kern_sched_rr**：默认兜底调度类，总是第一个注册
- **kern_sched_fifo**：抢占调度类，仅在 `CONFIG_PREEMPT_ENABLED` 时注册

---

> **See Also:** [Round-Robin 类](kern-sched-rr.md) | [优先级 FIFO 类](kern-sched-fifo.md) | [调度器](kern-task.md) | [SMP 支持](kern-smp.md)
