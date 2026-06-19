# 内核可移植层（Kern Port）

> **Parent:** [内核总览](index.md) | **Related:** [调度器](kern-task.md), [SMP 多核](kern-smp.md), [类型系统](kern-types.md)

## 概述

`kern_port` 是 Xeros 内核的**底层执行上下文抽象层**，将所有平台相关的线程创建、上下文切换、栈管理、定时器操作隔离在单一模块中。

自 kernel-v2-phase1 起，可移植层引入了 **`kern_port_ops_t` 多态操作表**，通过函数指针分派实现编译期后端切换。内核其他模块（`kern_task` 等）仅通过 `kern_port.h` 中的 `static inline` 包装器调用，不直接依赖 FreeRTOS 或任何特定调度原语。

---

## 设计目标

1. **零 FreeRTOS 渗透**：除 `kern_port_freertos.c` 外，内核所有源文件不包含任何 FreeRTOS 头文件
2. **三后端可切换**：FreeRTOS 任务容器 / 原生 setjmp/longjmp / Native 测试桩
3. **编译期选择**：通过 `#ifdef` 守卫在编译时选定后端，运行时通过 `g_kern_port_ops` 分派，无虚函数开销

---

## kern_port_ops_t 多态操作表

*📄 Source: [kern_port.h](../../src/kernel/kern_port.h#L68-L91)*

```c
typedef struct kern_port_ops {
    /* 生命周期 */
    void  (*init)(void);

    /* 线程管理 */
    kern_port_thread_t (*thread_spawn)(
        void (*entry)(void*), void *arg, const char *name,
        size_t stack_size, struct kern_task *task);
    void  (*thread_exit)(void);
    void  (*thread_kill)(kern_port_thread_t);
    size_t (*thread_stack_usage)(kern_port_thread_t);

    /* 上下文切换 */
    void  (*switch_to)(struct kern_task *task);
    void  (*task_yield)(void);
    void  (*task_exit)(void);

    /* 空闲处理 */
    void  (*idle)(void);

    /* 定时器基础设施（抢占式调度用） */
    int   (*timer_set_periodic)(uint32_t period_us);
    void  (*timer_stop)(void);
    bool  (*preempt_consume)(void);  /* 检查并消费 ISR 抢占 tick 请求 */
} kern_port_ops_t;

extern const kern_port_ops_t g_kern_port_ops;  /* 全局操作表实例 */
```

每个静态 `static inline` 包装器直接调用 `g_kern_port_ops` 中对应的函数指针。例如：

```c
static inline void kern_port_idle(void)
{
    g_kern_port_ops.idle();  // 编译时不可知调用哪个实现，运行时无分支
}
```

---

## 三后端架构对比

```
kern_port.h (接口层)
    │  static inline 包装器 → g_kern_port_ops.xxx()
    ├── kern_port_freertos.c     ← 默认（ESP32 + FreeRTOS）
    │       #if !NATIVE_TEST && !XEROS_NATIVE_SCHED
    │
    ├── kern_port_freertos.c     ← 原生调度器桩（占位）
    │       #if !NATIVE_TEST && XEROS_NATIVE_SCHED
    │
    └── kern_port_freertos.c     ← Native 测试空桩
            #if NATIVE_TEST
```

> 注意：三个后端实测都在同一个 `kern_port_freertos.c` 文件中，通过编译守卫 `#ifndef NATIVE_TEST` / `#elif XEROS_NATIVE_SCHED` / `#else` / `#else` 分离。`kern_port_native.c` 是早期实验文件，当前不参与编译。

| 特性 | FreeRTOS (`_freertos`) | 原生调度器桩 (`_native_sched`) | Native 测试 (`_native_test`) |
|------|------------------------|-------------------------------|---------------------------|
| 线程模型 | 1 Xeros 任务 = 1 FreeRTOS 任务 | 调度逻辑在 kern_task.c | 单线程 + ucontext |
| 栈管理 | FreeRTOS 自动管理 | 空桩（占位） | 空桩（占位） |
| 上下文切换 | 双二值信号量 give/take | 空桩 | 空桩 |
| 定时器 | ESP32 timer_group0 硬件定时器 | 空桩 | 空桩 |
| FreeRTOS 依赖 | 仅此文件（xTaskCreate/vTaskDelete/xSemaphore） | **零** | **零** |
| 稳定性 | ✅ 生产可用 | ⚠️ 实验性桩 | ✅ 测试用 |

---

## FreeRTOS 后端：双信号量令牌协议

*📄 Source: [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L309-L332)*

```
调度器（loop）                任务（wrapper）
─────────────                ───────────────
                              take(token) ← 阻塞等令牌
kern_sched_tick()
  pick_next_ready()
  kern_port_switch_to(task)
    give(token) ──────────→  获得令牌
    take(done) ← 阻塞        task->entry(arg)
                              give(done) ──→ 调度器解除阻塞
                              take(token) ← 再次阻塞
  take(done) 返回
  下一轮 tick...
```

### 中文伪代码拆解

```
/* 调度器侧：kern_port_freertos_switch_to */
函数 调度器切换到(任务) {
    给令牌信号量(token)         // 唤醒目标任务
    等待完成信号量(done, 5秒超时) // 阻塞直到任务完成时间片
    if (超时)
        标记任务为僵尸状态
}

/* 任务侧：kern_port_freertos_task_yield */
函数 任务让出CPU() {
    发完成信号量(done)          // 通知调度器本时间片结束
    等待令牌信号量(token)       // 阻塞等待下一轮分配
}

/* 任务侧：kern_port_freertos_task_exit */
函数 任务退出() {
    发完成信号量(done)          // 通知调度器任务结束
    删除当前FreeRTOS任务()      // vTaskDelete — 不会返回
}
```

---

## 抢占式定时器基础设施

*📄 Source: [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L131-L178)*

ESP32 抢占式调度通过硬件定时器（ESP32 `timer_group0/timer0`）驱动：

| 参数 | 值 | 说明 |
|------|-----|------|
| 时钟分频 | 80 | 80 MHz / 80 = 1 MHz → 1 tick = 1 μs |
| 周期 | 由调用者指定 | 典型值 1 ms（1000 μs） |
| 中断模式 | LEVEL（电平触发） | 配合 `auto_reload` 实现周期性 |
| ISR 属性 | `IRAM_ATTR` | 中断函数置于 IRAM 保证响应速度 |

定时器 ISR 调用调度器的回调函数（如 `kern_sched_tick`），实现硬件定时抢占。

## 抢占 tick 消费

*📄 Source: [kern_port.h](../../src/kernel/kern_port.h#L226-L235) 与 [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L169-L178)*

```c
static inline bool kern_port_preempt_consume(void)
{
    return g_kern_port_ops.preempt_consume();
}
```

在 ESP32 FreeRTOS 后端中，硬件定时器 ISR 只设置一个原子标志 `g_preempt_tick_pending`，不做任何调度逻辑。`kern_port_preempt_consume()` 在 `loop()` 或调度循环上下文中检查该标志：如果为真则清零并返回 `true`，调用者随后触发一次 `kern_sched_tick()`；否则返回 `false`。

```
函数 抢占消费() {
    if (无待处理抢占标志) return false

    清零抢占标志
    return true    // 调用者应执行一次调度 tick
}
```

这样将 ISR 保持最小化，避免在中断中执行链表遍历、内存释放等不可预测长度的操作。

---

## FreeRTOS 依赖收敛

去 FreeRTOS 前后的依赖分布对比：

```
Before (Phase 0-5):
  kern_init.c   ██████  6 处 FreeRTOS API
  kern_task.c   ████████████████████████  27 处
  kern_task.h   ███  3 处

After (kernel-v2-phase1):
  kern_port_freertos.c   ██████████████████████  所有 FreeRTOS 集中于此
  kern_init.c   0 处 ✅（自旋锁替代信号量）
  kern_task.c   0 处 ✅
  kern_task.h   0 处 ✅
```

**现在整个内核只有一个文件 (`kern_port_freertos.c`) 直接依赖 FreeRTOS**。切换到原生调度器时替换此文件即可。

---

## API 速查

| 函数 | 方向 | 说明 |
|------|------|------|
| `kern_port_init()` | — | 初始化可移植层 |
| `kern_port_thread_spawn()` | — | 创建新的执行上下文 |
| `kern_port_thread_exit()` | 任务→调度器 | 销毁当前线程（不返回） |
| `kern_port_thread_kill()` | — | 从外部销毁指定线程 |
| `kern_port_thread_stack_usage()` | — | 获取栈使用量 |
| `kern_port_switch_to(task)` | 调度器→任务 | 调度器切换到目标任务（阻塞等待） |
| `kern_port_task_yield()` | 任务→调度器 | 任务主动让出 CPU |
| `kern_port_task_exit()` | 任务→调度器 | 任务退出并销毁（不返回） |
| `kern_port_idle()` | — | 无就绪任务时的空闲处理 |
| `kern_port_timer_set_periodic()` | — | 启动硬件定时器（抢占式用） |
| `kern_port_timer_stop()` | — | 停止硬件定时器 |
| `kern_port_preempt_consume()` | — | 检查并消费 ISR 抢占 tick 请求 |

---

> **See Also:** [调度器与任务管理](kern-task.md) | [SMP 多核支持](kern-smp.md) | [内核初始化](kern-init.md) | [类型系统](kern-types.md)
