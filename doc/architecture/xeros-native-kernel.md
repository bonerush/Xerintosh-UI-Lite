# Xeros 原生内核架构设计

> **Parent:** [知识地图](../index.md) | **Related:** [上下文切换](context-switch.md) | [调度器](scheduler.md) | [IPC](ipc-primitives.md)

## 概述

本文档描述 Xeros 内核原生后端的完整架构设计。目标是创建一个完全自主的执行后端，替换当前的 FreeRTOS 依赖，同时保持 `kern_port_ops_t` 接口契约不变。

**核心设计决策：使用 Xtensa call0 ABI 进行上下文切换。**

## 设计原则

1. **FreeRTOS 兼容性**: 通过 `kern_port_ops_t` 操作表，FreeRTOS 后端和原生后端可以在编译时切换
2. **最小侵入**: 不修改现有内核模块的公共接口，只替换后端实现
3. **ISR 安全**: 所有 IPC 原语提供 `_from_isr` 变体，遵循延迟中断处理模式
4. **SMP 原生**: 每个核心运行独立的调度器循环，通过 IPI 实现跨核唤醒

## 架构组件图

```
┌──────────────────────────────────────────────────────────────────────┐
│                     Xeros Kernel (v3.0.0)                            │
│                                                                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────────┐        │
│  │ kern_task │  │kern_sched│  │kern_sync │  │ kern_kmalloc  │        │
│  │ lifecycle │  │  classes │  │ IPC layer│  │  mem alloc    │        │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └──────┬────────┘        │
│       │             │             │               │                  │
│  ┌────┴─────────────┴─────────────┴───────────────┴────────────┐     │
│  │              kern_port_ops_t (可插拔接口)                    │     │
│  └────────────────────────┬────────────────────────────────────┘     │
│                           │                                          │
├───────────────────────────┼──────────────────────────────────────────┤
│            原生后端 (kern_port_esp32_native.c)                      │
│                           │                                          │
│  ┌────────────────┐  ┌───────────┐  ┌──────────────┐                │
│  │  Context Sw.   │  │ Scheduler │  │  Tick Timer  │                │
│  │  (ctx_switch.S)│  │   Loop    │  │  (TG0/TIMG0) │                │
│  │  call0 ABI     │  │  per-CPU  │  │  ISR-driven  │                │
│  └────────────────┘  └───────────┘  └──────────────┘                │
│                                                                      │
│  ┌────────────────┐  ┌───────────┐  ┌──────────────┐                │
│  │  Critical      │  │   IPC     │  │   Idle/WFI   │                │
│  │  Sections      │  │ Primitives│  │   Low Power  │                │
│  │  (PS.INTMASK)  │  │ sem/mutex │  │              │                │
│  └────────────────┘  └───────────┘  └──────────────┘                │
│                                                                      │
│  ┌────────────────┐  ┌───────────┐                                   │
│  │  Debug Infra   │  │  Memory   │                                   │
│  │  /dev/ttyS0    │  │  Mgmt     │                                   │
│  └────────────────┘  └───────────┘                                   │
│                                                                      │
├──────────────────────────────────────────────────────────────────────┤
│              Hardware Abstraction Layer                               │
│  ESP32 Xtensa LX6  │  Timer Groups  │  Interrupt Controller         │
└──────────────────────────────────────────────────────────────────────┘
```

## 关键设计决策

### 1. 为什么选择 call0 ABI？

Xtensa 窗口 ABI 使用 64 个物理寄存器的滑动窗口，每次只能看到 16 个。切换栈时，newlib 的 setjmp 会从 SP 相对的保存区读取数据，这些数据在栈切换后会失效。

**call0 ABI 的优势：**
- 固定寄存器分配（无窗口旋转）
- 上下文保存/恢复操作绝对寄存器地址
- 栈切换安全，不受寄存器窗口状态影响
- 上下文切换代码更简单、更可靠

**call0 ABI 的代价：**
- 需要编译器标志 `-mabi=call0`
- 与 ESP-IDF 库的窗口 ABI 不兼容
- 所有原生后端代码必须使用 call0 编译

### 2. SMP 策略

每个核心运行独立的调度器循环。跨核任务迁移使用无锁 IPI（处理器间中断）机制。

```
Core 0 (PRO_CPU)          Core 1 (APP_CPU)
┌─────────────────┐      ┌─────────────────┐
│ Scheduler Loop  │      │ Scheduler Loop  │
│ kern_sched_tick │      │ kern_sched_tick │
│ waiti 0 (idle)  │      │ waiti 0 (idle)  │
└─────────────────┘      └─────────────────┘
         │                        │
         └───── IPI (cross-core) ─┘
```

### 3. IPC 设计

所有 IPC 原语遵循 FreeRTOS 命名约定，提供开发者熟悉感。每个原语都有 `_from_isr` 变体用于延迟中断处理。

| 原语 | 用途 | 关键特性 |
|------|------|----------|
| 二值信号量 | 任务间/ISR 到任务信号 | init, take, give, give_from_isr |
| 计数信号量 | 资源池管理 | init(max), take, give, get_count |
| 互斥锁 | 互斥访问 + 优先级继承 | init, lock, unlock, PI 协议 |
| 消息队列 | 任务间消息传递 | init(size, count), send, recv |
| 事件组 | 多条件同步 | set_bits, wait_bits(AND/OR) |

### 4. 中断边界

```
ISR 上下文                    任务上下文
┌─────────────────┐          ┌─────────────────┐
│ 1. 清除中断标志  │          │ 高优先级任务处理  │
│ 2. 发送信号量    │ ────────→│ 真正耗时的工作    │
│ 3. 设置 need_resched │      │                 │
└─────────────────┘          └─────────────────┘
```

ISR 只做最少量工作，真正耗时的处理交给高优先级任务。`xHigherPriorityTaskWoken` 等价机制通过 `need_resched` 标志实现。

## 文件结构

### 新建文件

| 文件路径 | 描述 | 复杂度 |
|----------|------|--------|
| `src/kernel/esp32/ctx_switch.h` | 上下文结构定义 | 中 |
| `src/kernel/esp32/ctx_switch.S` | Xtensa 汇编上下文切换 | 极高 |
| `src/kernel/esp32/ctx_init.c` | 新任务上下文初始化 | 高 |
| `src/kernel/esp32/tick_timer.h` | 定时器接口 | 中 |
| `src/kernel/esp32/tick_timer.c` | 硬件定时器实现 | 高 |
| `src/kernel/esp32/smp_native.h` | 原生 SMP 接口 | 中 |
| `src/kernel/esp32/smp_native.c` | 原生 CPU ID 和 IPI | 高 |
| `src/kernel/esp32/mem_native.h` | 原生内存接口 | 低 |
| `src/kernel/esp32/mem_native.c` | 原生堆信息 | 中 |
| `src/kernel/kern_port_esp32_native.c` | 原生后端主文件 | 极高 |
| `src/kernel/kern_port_esp32_native.h` | 原生后端头文件 | 低 |
| `src/kernel/kern_ipc.h` | IPC 原语接口 | 高 |
| `src/kernel/kern_ipc.c` | 信号量和互斥锁实现 | 高 |
| `src/kernel/kern_ipc_queue.h` | 消息队列接口 | 中 |
| `src/kernel/kern_ipc_queue.c` | 消息队列实现 | 中 |
| `src/kernel/kern_ipc_event.h` | 事件组接口 | 中 |
| `src/kernel/kern_ipc_event.c` | 事件组实现 | 中 |
| `src/kernel/debug/kern_debug.h` | 调试框架接口 | 中 |
| `src/kernel/debug/kern_debug.c` | 调试框架实现 | 中 |
| `src/kernel/debug/kern_task_inspector.c` | 任务检查器 | 低 |
| `src/kernel/debug/kern_sched_trace.c` | 调度追踪 | 中 |
| `src/kernel/debug/kern_mem_profiler.c` | 内存分析器 | 中 |
| `test/test_native/test_ctx_switch.cpp` | 上下文切换测试 | 中 |
| `test/test_native/test_sched_native.cpp` | 调度器测试 | 中 |
| `test/test_native/test_ipc_native.cpp` | IPC 测试 | 中 |
| `test/test_native/test_smp_native.cpp` | SMP 测试 | 中 |
| `test/test_native/test_benchmark.cpp` | 性能基准测试 | 中 |

### 修改文件

| 文件路径 | 修改内容 |
|----------|----------|
| `src/kernel/kern_task.h` | 添加 native_ctx 和 native_stack 字段 |
| `src/kernel/kern_ctx_esp32.h` | 替换 setjmp 为原生上下文类型 |
| `src/kernel/kern_sched.c` | 更新调度器循环使用原生上下文切换 |
| `src/kernel/kern_task_lifecycle.c` | 更新 spawn/yield/exit 使用原生上下文 |
| `src/kernel/kern_smp.c` | 移除 FreeRTOS SMP 依赖 |
| `src/kernel/kern_smp.h` | 更新 SMP 宏定义 |
| `src/kernel/kern_sync.h` | 扩展同步原语接口 |
| `src/kernel/kern_sync.c` | 更新 mutex 实现支持优先级继承 |
| `src/kernel/kern_kmalloc.c` | 适配原生内存管理 |
| `src/kernel/kern_port.h` | 更新后端选择逻辑 |
| `src/kernel/kern_port_freertos.c` | 移除 XEROS_NATIVE_SCHED 桩代码 |
| `platformio.ini` | 添加原生构建环境 |
| `src/CMakeLists.txt` | 条件编译支持 |

## API 设计

### 上下文切换模块

```c
// ctx_switch.h
typedef struct {
    uint32_t a0, a1, a2, a3, a4, a5, a6, a7;
    uint32_t a8, a9, a10, a11, a12, a13, a14, a15;
    uint32_t sar, lbeg, lend, lcount;
    uint32_t ps, exccause, excvaddr;
    uint32_t pc;
} kern_ctx_native_t;

#define KERN_CTX_NATIVE_SIZE  (24 * sizeof(uint32_t))

// 汇编函数声明
void xeros_ctx_init(kern_ctx_native_t *ctx, void *sp, void *entry, void *arg);
int  xeros_ctx_save(kern_ctx_native_t *ctx);
void xeros_ctx_restore(kern_ctx_native_t *ctx);
void xeros_flush_windows(void);
```

### IPC 原语模块

```c
// kern_ipc.h - 二值信号量
typedef struct {
    spinlock_t    lock;
    bool          signaled;
    kern_task_t  *wait_queue;
} kern_bin_sem_t;

kern_err_t kern_bin_sem_init(kern_bin_sem_t *sem);
kern_err_t kern_bin_sem_take(kern_bin_sem_t *sem);
kern_err_t kern_bin_sem_take_timeout(kern_bin_sem_t *sem, uint32_t timeout_ms);
kern_err_t kern_bin_sem_give(kern_bin_sem_t *sem);
kern_err_t kern_bin_sem_give_from_isr(kern_bin_sem_t *sem, bool *woken);

// kern_ipc.h - 互斥锁（带优先级继承）
typedef struct {
    spinlock_t    lock;
    kern_task_t  *owner;
    uint8_t       recursive_count;
    uint8_t       original_priority;  // PI: 保存原始优先级
    kern_task_t  *wait_queue;
} kern_mutex_t;

kern_err_t kern_mutex_init(kern_mutex_t *m);
kern_err_t kern_mutex_lock(kern_mutex_t *m);
kern_err_t kern_mutex_unlock(kern_mutex_t *m);

// kern_ipc_queue.h - 消息队列
typedef struct {
    spinlock_t    lock;
    uint8_t      *buffer;
    uint32_t      item_size;
    uint32_t      capacity;
    uint32_t      head, tail, count;
    kern_task_t  *send_waiters;
    kern_task_t  *recv_waiters;
} kern_queue_t;

kern_err_t kern_queue_init(kern_queue_t *q, uint32_t item_size, uint32_t capacity);
kern_err_t kern_queue_send(kern_queue_t *q, const void *item);
kern_err_t kern_queue_recv(kern_queue_t *q, void *item);

// kern_ipc_event.h - 事件组
typedef struct {
    spinlock_t    lock;
    uint32_t      flags;
    kern_task_t  *wait_queue;
} kern_event_t;

kern_err_t kern_event_init(kern_event_t *ev);
kern_err_t kern_event_set(kern_event_t *ev, uint32_t bits);
kern_err_t kern_event_wait(kern_event_t *ev, uint32_t bits, bool wait_all, uint32_t timeout_ms);
```

### 原生后端操作表

```c
// kern_port_esp32_native.c
const kern_port_ops_t g_kern_port_ops = {
    .init                = native_init,
    .thread_spawn        = native_thread_spawn,
    .thread_exit         = native_thread_exit,
    .thread_kill         = native_thread_kill,
    .thread_stack_usage  = native_thread_stack_usage,
    .switch_to           = native_switch_to,
    .task_yield          = native_task_yield,
    .task_exit           = native_task_exit,
    .idle                = native_idle,          // waiti 0
    .timer_set_periodic  = native_timer_set,
    .timer_stop          = native_timer_stop,
    .preempt_consume     = native_preempt_consume,
};
```

## 构建系统变更

### platformio.ini

```ini
[env:m5stick-c-native]
platform = espressif32
board = m5stick-c
framework = espidf
build_flags =
    -std=c++17
    -DXEROS_NATIVE_SCHED
    -mabi=call0
```

### 编译时后端选择

```c
// kern_port.h
#ifdef XEROS_NATIVE_SCHED
#include "kern_port_esp32_native.h"
// g_kern_port_ops 由 kern_port_esp32_native.c 定义
#else
#include "kern_port_freertos.h"
// g_kern_port_ops 由 kern_port_freertos.c 定义
#endif
```

## 迁移策略

### 阶段 1: 并行开发
- 原生后端与 FreeRTOS 后端同时存在
- 通过 `XEROS_NATIVE_SCHED` 编译标志切换
- 所有现有测试继续通过

### 阶段 2: 功能对等
- 原生后端实现所有 FreeRTOS 后端的功能
- 在硬件上运行完整应用验证

### 阶段 3: 性能优化
- 基准测试比较两个后端
- 优化原生后端的热路径

### 阶段 4: 默认切换
- 将原生后端设为默认
- FreeRTOS 后端保留为备选

---

> **See Also:** [上下文切换](context-switch.md) | [调度器](scheduler.md) | [IPC](ipc-primitives.md) | [实施计划](../implementation-plan.md)
