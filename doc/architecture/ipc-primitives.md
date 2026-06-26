# IPC 任务间通信设计

> **Parent:** [原生内核架构](xeros-native-kernel.md) | **Related:** [调度器](scheduler.md) | [中断边界](interrupt-boundary.md)

## 概述

本文档描述 Xeros 内核的 IPC（进程间通信）原语设计。当前内核仅有 spinlock 和递归 mutex，需要扩展为完整的 IPC 子系统，包括二值信号量、计数信号量、优先级继承互斥锁、消息队列和事件组。

## 当前状态

*📄 Source: [kern_sync.h](../../src/kernel/kern_sync.h)*

| 原语 | 状态 | 说明 |
|------|------|------|
| spinlock | ✅ 已有 | GCC 原子操作，SMP 感知 |
| mutex | ⚠️ 不完整 | 有递归加锁，但无优先级继承，busy-spin 阻塞 |
| 二值信号量 | ❌ 缺失 | 仅在 FreeRTOS 端口层内部使用 |
| 计数信号量 | ❌ 缺失 | - |
| 消息队列 | ❌ 缺失 | - |
| 事件组 | ❌ 缺失 | - |

## 设计原则

1. **FreeRTOS 命名约定**: 使用开发者熟悉的 API 命名
2. **ISR 安全**: 所有原语提供 `_from_isr` 变体
3. **阻塞而非忙等**: 使用等待队列 + yield，不使用 spin-loop
4. **优先级继承**: mutex 自动实现优先级继承协议

## 二值信号量 (Binary Semaphore)

### 数据结构

```c
typedef struct {
    spinlock_t    lock;
    bool          signaled;       // 当前状态
    kern_task_t  *wait_queue;     // 等待队列（按优先级排序）
} kern_bin_sem_t;
```

### API

```c
kern_err_t kern_bin_sem_init(kern_bin_sem_t *sem);
kern_err_t kern_bin_sem_take(kern_bin_sem_t *sem);           // 阻塞等待
kern_err_t kern_bin_sem_trytake(kern_bin_sem_t *sem);        // 非阻塞尝试
kern_err_t kern_bin_sem_take_timeout(kern_bin_sem_t *sem, uint32_t ms);
kern_err_t kern_bin_sem_give(kern_bin_sem_t *sem);           // 唤醒一个等待者
kern_err_t kern_bin_sem_give_from_isr(kern_bin_sem_t *sem, bool *woken);
```

### 实现逻辑

```
take():
    spinlock_lock(&sem->lock)
    if (sem->signaled) {
        sem->signaled = false     // 消费信号
        spinlock_unlock(&sem->lock)
        return KERN_OK
    }
    // 加入等待队列
    当前任务->state = KERN_TASK_BLOCKED
    加入等待队列（按优先级排序）
    spinlock_unlock(&sem->lock)
    kern_port_task_yield()        // 让出 CPU
    return KERN_OK                // 被唤醒后返回

give():
    spinlock_lock(&sem->lock)
    if (有等待者) {
        从等待队列取出最高优先级任务
        任务->state = KERN_TASK_READY
        加入就绪队列
        if (唤醒的任务优先级 > 当前任务)
            设置 need_resched = true
    } else {
        sem->signaled = true      // 无人等待，设置标志
    }
    spinlock_unlock(&sem->lock)

give_from_isr():
    spinlock_lock(&sem->lock)
    if (有等待者) {
        从等待队列取出最高优先级任务
        任务->state = KERN_TASK_READY
        加入就绪队列
        *woken = true             // 通知 ISR 需要上下文切换
    } else {
        sem->signaled = true
    }
    spinlock_unlock(&sem->lock)
    // 注意：不在 ISR 中执行上下文切换
    // 由 ISR 返回前检查 woken 标志决定
```

### 中文伪代码拆解

```
函数 kern_bin_sem_take(信号量) {
    自旋锁加锁(信号量.锁)

    if (信号量.已触发) {
        信号量.已触发 = false    // 消费信号
        自旋锁解锁(信号量.锁)
        return 成功
    }

    // 没有信号，需要等待
    当前任务.状态 = 阻塞中
    加入等待队列(信号量.等待队列, 当前任务)  // 按优先级排序
    自旋锁解锁(信号量.锁)

    让出CPU()   // 切换回调度器
    // 被唤醒后从这里继续
    return 成功
}
```

## 计数信号量 (Counting Semaphore)

### 数据结构

```c
typedef struct {
    spinlock_t    lock;
    uint32_t      count;          // 当前计数
    uint32_t      max_count;      // 最大计数
    kern_task_t  *wait_queue;
} kern_sem_t;
```

### API

```c
kern_err_t kern_sem_init(kern_sem_t *sem, uint32_t max_count);
kern_err_t kern_sem_take(kern_sem_t *sem);
kern_err_t kern_sem_give(kern_sem_t *sem);
uint32_t   kern_sem_get_count(kern_sem_t *sem);
```

### 实现逻辑

```
take():
    if (count > 0) {
        count--
        return KERN_OK
    }
    // count == 0，阻塞
    加入等待队列
    yield()

give():
    if (count < max_count) {
        count++
    }
    if (有等待者) {
        唤醒一个等待者（count-- 由被唤醒的任务执行）
    }
```

## 互斥锁 (Mutex with Priority Inheritance)

### 数据结构

```c
typedef struct {
    spinlock_t    lock;
    kern_task_t  *owner;              // 当前持有者
    uint8_t       recursive_count;    // 递归加锁计数
    uint8_t       original_priority;  // PI: 原始优先级
    kern_task_t  *wait_queue;         // 等待队列
} kern_mutex_t;
```

### 优先级继承协议

```
场景：任务 L（低优先级）持有 mutex，任务 H（高优先级）尝试获取

1. H 调用 mutex_lock()
2. 发现 owner = L，且 L.priority < H.priority
3. 提升 L.priority = H.priority（优先级继承）
4. H 加入等待队列，yield()

5. L 继续运行（现在以 H 的优先级运行）
6. L 调用 mutex_unlock()
7. 恢复 L.priority = L.original_priority
8. 唤醒等待队列中最高优先级的任务
9. 如果唤醒的任务优先级 > 当前任务，触发 resched
```

### 中文伪代码拆解

```
函数 kern_mutex_lock(互斥锁) {
    自旋锁加锁(互斥锁.锁)

    // 情况1：自己已经持有（递归加锁）
    if (互斥锁.持有者 == 当前任务) {
        互斥锁.递归计数++
        自旋锁解锁(互斥锁.锁)
        return 成功
    }

    // 情况2：无人持有
    if (互斥锁.持有者 == NULL) {
        互斥锁.持有者 = 当前任务
        互斥锁.递归计数 = 1
        互斥锁.原始优先级 = 当前任务.优先级
        自旋锁解锁(互斥锁.锁)
        return 成功
    }

    // 情况3：被其他任务持有，需要等待
    // 优先级继承：如果当前任务优先级更高，提升持有者优先级
    if (当前任务.优先级 > 互斥锁.持有者.优先级) {
        互斥锁.持有者.优先级 = 当前任务.优先级
    }

    当前任务.状态 = 阻塞中
    加入等待队列(互斥锁.等待队列, 当前任务)
    自旋锁解锁(互斥锁.锁)

    让出CPU()
    return 成功
}

函数 kern_mutex_unlock(互斥锁) {
    自旋锁加锁(互斥锁.锁)

    if (互斥锁.持有者 != 当前任务) {
        自旋锁解锁(互斥锁.锁)
        return 权限错误
    }

    互斥锁.递归计数--

    if (互斥锁.递归计数 == 0) {
        // 恢复原始优先级
        当前任务.优先级 = 互斥锁.原始优先级

        if (有等待者) {
            最高优先级等待者 = 取出最高优先级(互斥锁.等待队列)
            互斥锁.持有者 = 最高优先级等待者
            互斥锁.递归计数 = 1
            互斥锁.原始优先级 = 最高优先级等待者.优先级
            最高优先级等待者.状态 = 就绪中
            加入就绪队列(最高优先级等待者)

            if (最高优先级等待者.优先级 > 当前任务.优先级) {
                设置需要重调度()
            }
        } else {
            互斥锁.持有者 = NULL
        }
    }

    自旋锁解锁(互斥锁.锁)
}
```

## 消息队列 (Message Queue)

### 数据结构

```c
typedef struct {
    spinlock_t    lock;
    uint8_t      *buffer;         // 环形缓冲区
    uint32_t      item_size;      // 每个消息的大小（字节）
    uint32_t      capacity;       // 最大消息数
    uint32_t      head;           // 读位置
    uint32_t      tail;           // 写位置
    uint32_t      count;          // 当前消息数
    kern_task_t  *send_waiters;   // 发送等待队列（满时）
    kern_task_t  *recv_waiters;   // 接收等待队列（空时）
} kern_queue_t;
```

### API

```c
kern_err_t kern_queue_init(kern_queue_t *q, uint32_t item_size, uint32_t capacity);
void       kern_queue_destroy(kern_queue_t *q);

kern_err_t kern_queue_send(kern_queue_t *q, const void *item);
kern_err_t kern_queue_send_timeout(kern_queue_t *q, const void *item, uint32_t ms);
kern_err_t kern_queue_try_send(kern_queue_t *q, const void *item);
kern_err_t kern_queue_send_from_isr(kern_queue_t *q, const void *item, bool *woken);

kern_err_t kern_queue_recv(kern_queue_t *q, void *item);
kern_err_t kern_queue_recv_timeout(kern_queue_t *q, void *item, uint32_t ms);
kern_err_t kern_queue_try_recv(kern_queue_t *q, void *item);
kern_err_t kern_queue_recv_from_isr(kern_queue_t *q, void *item, bool *woken);

uint32_t   kern_queue_get_count(kern_queue_t *q);
```

### 环形缓冲区操作

```
send():
    spinlock_lock(&q->lock)
    if (count == capacity) {
        // 队列满，阻塞等待
        加入 send_waiters
        spinlock_unlock(&q->lock)
        yield()
        spinlock_lock(&q->lock)
    }
    memcpy(buffer + tail * item_size, item, item_size)
    tail = (tail + 1) % capacity
    count++
    if (有 recv_waiters) {
        唤醒一个接收等待者
    }
    spinlock_unlock(&q->lock)

recv():
    spinlock_lock(&q->lock)
    if (count == 0) {
        // 队列空，阻塞等待
        加入 recv_waiters
        spinlock_unlock(&q->lock)
        yield()
        spinlock_lock(&q->lock)
    }
    memcpy(item, buffer + head * item_size, item_size)
    head = (head + 1) % capacity
    count--
    if (有 send_waiters) {
        唤醒一个发送等待者
    }
    spinlock_unlock(&q->lock)
```

## 事件组 (Event Group)

### 数据结构

```c
typedef struct {
    spinlock_t    lock;
    uint32_t      flags;          // 当前事件标志
    kern_task_t  *wait_queue;     // 等待队列
} kern_event_t;

// 等待队列节点扩展（存储在 TCB 中）
typedef struct {
    uint32_t      wait_bits;      // 等待的位
    bool          wait_all;       // true=AND, false=OR
    bool          auto_clear;     // 唤醒后自动清除
} kern_event_waiter_t;
```

### API

```c
kern_err_t kern_event_init(kern_event_t *ev);
kern_err_t kern_event_set(kern_event_t *ev, uint32_t bits);
kern_err_t kern_event_clear(kern_event_t *ev, uint32_t bits);
kern_err_t kern_event_wait(kern_event_t *ev, uint32_t bits,
                            bool wait_all, uint32_t timeout_ms);
uint32_t   kern_event_get(kern_event_t *ev);
```

### 等待逻辑

```
wait(event, bits, wait_all, timeout):
    spinlock_lock(&event->lock)

    // 检查条件是否已满足
    if (wait_all) {
        satisfied = (event->flags & bits) == bits
    } else {
        satisfied = (event->flags & bits) != 0
    }

    if (satisfied) {
        if (auto_clear) {
            event->flags &= ~bits
        }
        spinlock_unlock(&event->lock)
        return KERN_OK
    }

    // 条件不满足，阻塞
    设置等待参数: wait_bits, wait_all, auto_clear
    加入等待队列
    spinlock_unlock(&event->lock)
    yield()

set(event, bits):
    spinlock_lock(&event->lock)
    event->flags |= bits

    // 扫描等待队列，检查哪些任务的条件满足
    遍历等待队列 {
        if (等待者的条件满足) {
            if (等待者.auto_clear) {
                event->flags &= ~等待者.wait_bits
            }
            从等待队列移除
            加入就绪队列
        }
    }

    spinlock_unlock(&event->lock)
```

## ISR 安全模式

所有 IPC 原语的 `_from_isr` 变体遵循相同的模式：

```c
kern_err_t kern_xxx_from_isr(kern_xxx_t *obj, ..., bool *woken)
{
    spinlock_lock(&obj->lock);

    // 执行操作（与非 ISR 版本相同）
    ...

    // 但不在 ISR 中执行上下文切换
    if (需要唤醒任务) {
        加入就绪队列
        *woken = true;  // 通知 ISR 需要上下文切换
    }

    spinlock_unlock(&obj->lock);
    return KERN_OK;
}

// ISR 使用模式
void my_isr(void *arg)
{
    bool woken = false;

    // 最少量工作
    清除中断标志();
    kern_bin_sem_give_from_isr(&my_sem, &woken);

    // ISR 返回前检查是否需要上下文切换
    if (woken) {
        g_need_resched = true;  // 通知调度器
    }
}
```

## 优先级继承实现细节

### 优先级提升

```c
static void pi_boost_priority(kern_mutex_t *m, kern_task_t *waiter)
{
    if (waiter->priority > m->owner->priority) {
        // 记录原始优先级（仅首次提升时）
        if (m->owner->priority == m->original_priority) {
            m->original_priority = m->owner->priority;
        }
        m->owner->priority = waiter->priority;
    }
}
```

### 优先级恢复

```c
static void pi_restore_priority(kern_mutex_t *m)
{
    m->owner->priority = m->original_priority;
}
```

### 死锁检测（可选）

```c
// 检测循环等待
static bool pi_detect_deadlock(kern_mutex_t *m, kern_task_t *waiter)
{
    kern_task_t *owner = m->owner;
    int depth = 0;

    while (owner != NULL && depth < KERN_MAX_TASKS) {
        if (owner == waiter) return true;  // 循环！

        // 查找 owner 正在等待的 mutex
        kern_mutex_t *waiting_on = find_mutex_owner_waiting_on(owner);
        if (waiting_on == NULL) break;
        owner = waiting_on->owner;
        depth++;
    }
    return false;
}
```

## 测试策略

### 单元测试

```cpp
// test_ipc_native.cpp

TEST(BinarySemaphore, GiveAndTake) {
    kern_bin_sem_t sem;
    kern_bin_sem_init(&sem);

    // 先 give 后 take
    kern_bin_sem_give(&sem);
    EXPECT_EQ(kern_bin_sem_trytake(&sem), KERN_OK);
    EXPECT_EQ(kern_bin_sem_trytake(&sem), KERN_ERR);  // 已消费
}

TEST(Mutex, PriorityInheritance) {
    kern_mutex_t m;
    kern_mutex_init(&m);

    // 低优先级任务持有 mutex
    kern_mutex_lock(&m);

    // 高优先级任务尝试获取（会阻塞）
    // 验证持有者优先级被提升
    EXPECT_EQ(m.owner->priority, HIGH_PRIORITY);
}
```

### 集成测试

```
优先级反转场景：
1. 创建任务 L（优先级 1）、M（优先级 2）、H（优先级 3）
2. L 获取 mutex
3. H 尝试获取 mutex（阻塞，L 的优先级提升到 3）
4. M 尝试运行（被 L 的提升优先级阻塞）
5. L 释放 mutex（优先级恢复为 1）
6. H 获取 mutex 并运行
7. 验证完成时间符合预期
```

---

> **See Also:** [原生内核架构](xeros-native-kernel.md) | [中断边界](interrupt-boundary.md)
