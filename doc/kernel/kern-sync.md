# 同步原语（Kern Sync）

> **Parent:** [内核总览](index.md) | **Related:** [SMP 支持](kern-smp.md), [调度器](kern-task.md)

## 概述

`kern_sync.h/c` 为 Xeros 内核提供**自旋锁（spinlock）**和**互斥锁（mutex）**两种同步原语。在 SMP 模式下使用 GCC 内建原子操作（`__sync_lock_test_and_set`）保护临界区；在单核模式下退化为空操作或简单检查，实现零开销。

**核心设计原则**：同一份 API 在 SMP/单核两种模式下提供语义正确的实现——SMP 模式保证多核安全，单核模式消除所有同步开销。

---

## 关键概念

### 自旋锁（spinlock_t）

*📄 Source: [kern_sync.h](../../src/kernel/kern_sync.h#L29-L44)*

```c
typedef struct {
    volatile bool locked;
} spinlock_t;

#ifdef CONFIG_SMP_ENABLED
void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
#else
#define spinlock_init(l)     do { (l)->locked = false; } while (0)
#define spinlock_lock(l)     do {} while (0)
#define spinlock_unlock(l)   do {} while (0)
#endif
```

#### SMP 模式实现

*📄 Source: [kern_sync.c](../../src/kernel/kern_sync.c#L18-L34)*

```c
void spinlock_init(spinlock_t *lock)
{
    lock->locked = false;
}

void spinlock_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, true)) {
        __asm__ volatile("nop");  /* 自旋等待 + CPU 降功耗 */
    }
}

void spinlock_unlock(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}
```

#### 中文伪代码拆解

```
函数 自旋锁_加锁(锁指针) {
    /* 原子操作：尝试将 locked 从 false 设为 true
     * __sync_lock_test_and_set 返回旧值：
     *   - 旧值 = false → 获取成功（无人持有）
     *   - 旧值 = true  → 已被持有，继续自旋
     */
    循环 {
        if (原子测试并设置(&锁->locked, true) == false) {
            获取成功，退出
        }
        执行 NOP 指令  /* 降低总线争用 + 省电 */
    }
}

函数 自旋锁_解锁(锁指针) {
    原子释放(&锁->locked)  /* 将 locked 设为 false */
}

/* 关键点：
 * - __sync_lock_test_and_set 是 GCC 内建函数，编译为硬件原子指令
 * - 自旋时插入 nop 降低功耗并减少总线争用
 * - 自旋锁不应长时间持有（临界区应 < 10us）
 */
```

### 自旋锁零开销退化

```
SMP 模式:
    spinlock_lock(&lock)
    → while (__sync_lock_test_and_set(...)) { nop; }
    → 编译为: 原子交换指令 + 条件跳转 + nop 循环
    → 开销: ~10-50 个 CPU 周期（无竞争）/ 可变（有竞争）


单核模式 (CONFIG_SMP_ENABLED 未定义):
    spinlock_lock(&lock)
    → do {} while (0)
    → 编译为: (空)
    → 开销: 0 指令
```

### 互斥锁（mutex_t）

*📄 Source: [kern_sync.h](../../src/kernel/kern_sync.h#L54-L86)*

```c
typedef struct {
    spinlock_t    lock;        /* 内部自旋锁 */
    kern_task_t  *owner;       /* 当前持有者 */
    kern_task_t  *wait_queue;  /* 等待队列头（预留） */
} mutex_t;
```

#### SMP 模式实现

*📄 Source: [kern_sync.c](../../src/kernel/kern_sync.c#L38-L81)*

```c
void mutex_lock(mutex_t *m)
{
    kern_task_t *self = g_current_task;

    spinlock_lock(&m->lock);   /* 第1步：获取内部自旋锁 */

    if (m->owner == NULL) {    /* 第2步：无人持有，直接获取 */
        m->owner = self;
        spinlock_unlock(&m->lock);
    } else if (m->owner == self) {
        /* 第3步：递归获取（允许但记录警告） */
        spinlock_unlock(&m->lock);
    } else {
        /* 第4步：已被其他任务持有，自旋等待 */
        spinlock_unlock(&m->lock);
        while (1) {
            spinlock_lock(&m->lock);
            if (m->owner == NULL) {
                m->owner = self;
                spinlock_unlock(&m->lock);
                return;
            }
            spinlock_unlock(&m->lock);
            __asm__ volatile("nop");
        }
    }
}

void mutex_unlock(mutex_t *m)
{
    spinlock_lock(&m->lock);
    m->owner = NULL;
    spinlock_unlock(&m->lock);
}
```

#### 中文伪代码拆解

```
函数 互斥锁_加锁(锁指针) {
    我自己 = g_current_task  /* per-CPU 宏，获取当前 CPU 上的任务 */

    自旋锁_加锁(&锁->内部自旋锁)  /* 保护 owner 字段的原子性 */

    if (锁->持有者 == NULL) {
        /* 情况1：锁空闲，直接获取 */
        锁->持有者 = 我自己
        自旋锁_解锁(&锁->内部自旋锁)
    } else if (锁->持有者 == 我自己) {
        /* 情况2：我已经持有（递归），允许但警告 */
        自旋锁_解锁(&锁->内部自旋锁)
    } else {
        /* 情况3：被其他任务持有，自旋等待释放 */
        自旋锁_解锁(&锁->内部自旋锁)
        循环 {
            自旋锁_加锁(&锁->内部自旋锁)
            if (锁->持有者 == NULL) {
                锁->持有者 = 我自己
                自旋锁_解锁(&锁->内部自旋锁)
                return  /* 获取成功 */
            }
            自旋锁_解锁(&锁->内部自旋锁)
            NOP  /* 降低争用 */
        }
    }
}

函数 互斥锁_解锁(锁指针) {
    自旋锁_加锁(&锁->内部自旋锁)
    锁->持有者 = NULL     /* 释放所有权 */
    自旋锁_解锁(&锁->内部自旋锁)
    /* 等待者会在下一次循环中检测到 owner==NULL 并获取 */
}
```

#### 互斥锁状态流转图

```
    初始状态 (owner=NULL)
           │
           ▼
    任务A 调用 mutex_lock()
           │
           ▼
    持有状态 (owner=任务A)
           │
           ├── 任务A 再次 mutex_lock()  →  递归（允许）
           │
           ├── 任务B 调用 mutex_lock()  →  自旋等待
           │    ┌──────────────────────────────┐
           │    │ 任务B 循环:                    │
           │    │   lock内部锁 → 检查owner → 则用│
           │    │   unlock内部锁 → nop          │
           │    └──────────────────────────────┘
           │
           └── 任务A 调用 mutex_unlock()
                    │
                    ▼
               owner=NULL → 任务B 获取成功
```

#### 单核模式退化

*📄 Source: [kern_sync.h](../../src/kernel/kern_sync.h#L68-L84)*

```c
static inline void mutex_init(mutex_t *m)
{
    spinlock_init(&m->lock);
    m->owner      = NULL;
    m->wait_queue = NULL;
}

static inline void mutex_lock(mutex_t *m)
{
    /* 单核无竞争，直接标记所有权 */
    m->owner = g_current_task;
}

static inline void mutex_unlock(mutex_t *m)
{
    m->owner = NULL;
}
```

#### 单核退化的设计意图

```
单核模式下的互斥锁实际上是一个"所有权标记"：

- 无需自旋（一个 CPU 不可能和自己争用）
- 无需原子操作（无并发访问可能）
- 保留 owner 指针用于调试和可观测性

这个简化版本在功能上相当于：
  "我声明我正在使用这个资源"

如果在单核模式下同一个任务重复 lock 同一个 mutex，
不会报错（直接覆盖 owner），这简化了单核内核的使用场景。
```

---

## 两种原语对比

| 特性 | spinlock_t | mutex_t |
|------|------------|---------|
| 内部结构 | `volatile bool locked` | `spinlock_t + owner指针 + 等待队列` |
| 持有者追踪 | 无 | 有（`owner` 字段） |
| 递归获取 | 不支持（死锁） | 允许（警告） |
| 等待行为 | 忙等（自旋） | 忙等（自旋 + 内部锁） |
| 单核退化 | 空操作 | 简单赋值 |
| 适用场景 | 极短临界区（<10行代码） | 较长临界区、需要所有权语义 |

---

## 与其他组件的关系

- **kern_smp**：`g_current_task` 宏确保 mutex 能正确获取当前 CPU 上的任务指针
- **kern_sched**：调度器在 tick 中可能持有自旋锁保护全局任务链表
- **kern_vfs**：多核并发访问 VFS 数据结构时需要使用 spinlock 保护

---

> **See Also:** [SMP 支持](kern-smp.md) | [调度器](kern-task.md) | [可插拔调度类](kern-sched-class.md)
