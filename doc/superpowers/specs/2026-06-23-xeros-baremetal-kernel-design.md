# Xeros 裸机内核重设计规格

> **目标**：彻底移除 FreeRTOS 依赖，为 ESP32 (Xtensa LX6) 构建 bare-metal 调度核心，保留 Xeros VFS/Device/HAL/App 上层不变。

> **方案**：调度核心重写（方案 B）— 从底层重建上下文切换和调度器，保持现有抽象层。

---

## 1. 架构概览

```
  不变          ┌──────────────┐
                │  App / UI    │
                ├──────────────┤
  不变          │  HAL 层      │
                ├──────────────┤
  不变          │  Xeros VFS   │
                ├──────────────┤
  小幅修改      │  kern_task   │  ← 进程/线程模型增强
                ├──────────────┤
  重写 ★★★     │  kern_sched  │  ← O(1) 优先级调度器
                ├──────┬───────┤
  新增 ★★★     │  kern_ctx    │  ← 完整上下文切换（汇编）
                ├──────┬───────┤
  新增 ★★★     │  kern_runq   │  ← Per-CPU 就绪队列 + bitmap
                ├──────┬───────┤
  新增 ★★★     │  kern_sync   │  ← spinlock/mutex/sem 裸机实现
                ├──────┬───────┤
  新增 ★★★     │  kern_timer  │  ← CCOMPARE0 定时器
                ├──────┬───────┤
  新增 ★★★     │  kern_smp    │  ← IPI 核间中断 + 亲和性
                └──────┴───────┘
```

**设计约束**：
- 彻底摆脱 FreeRTOS 任务/信号量/队列 API
- 保留 ESP-IDF 硬件驱动层（GPIO、SPI、I2C、heap_caps、NVS 等）
- ESP32 WiFi 栈通过 FreeRTOS shim 兼容层过渡
- Per-CPU 独立调度队列（无全局锁）
- 优先级抢占式调度 + 时间片轮转

---

## 2. 上下文切换 (`kern_ctx`)

### 2.1 上下文结构 (`kern_ctx_t`)

Xtensa LX6 完整寄存器上下文，约 80 字节：

```c
typedef struct kern_ctx {
    uint32_t pc;          // 程序计数器
    uint32_t ps;          // 程序状态字
    uint32_t a[16];       // A0-A15，全部 16 个通用寄存器
    uint32_t sar;         // 移位量寄存器
    uint32_t lbeg;        // 循环起始地址
    uint32_t lend;        // 循环结束地址
    uint32_t lcount;      // 循环计数
    uint32_t sp_shadow;   // A1 副本（调试用，冗余但便于栈追踪）
} kern_ctx_t;
```

### 2.2 汇编函数接口

| 函数 | 签名 | 作用 |
|------|------|------|
| `kern_ctx_switch` | `void kern_ctx_switch(kern_ctx_t *from, kern_ctx_t *to)` | 保存当前到 `*from`，从 `*to` 恢复 |
| `kern_ctx_init` | `void kern_ctx_init(kern_ctx_t *ctx, void (*entry)(void*), void *arg, void *stack_top)` | 初始化新上下文：PC=entry, SP=stack_top, A2=arg |
| `kern_ctx_switch_isr` | `void kern_ctx_switch_isr(kern_ctx_t *from, kern_ctx_t *to)` | ISR 返回时使用：部分寄存器由硬件自动保存 |

### 2.3 切换流程

```
任务 A (运行中)
  │ kern_yield() / 抢占
  ├─ kern_ctx_switch(&A.ctx, &idle.ctx)
  │    ├─ S32I 保存 A0-A15, SAR, LBEG/LEND/LCOUNT, PS, PC
  │    └─ L32I 恢复 idle 上下文 → 跳转到调度循环
  │
  ▼ 调度循环（运行在 idle 任务栈上）
  │ pick_next() → 选择任务 B
  ├─ kern_ctx_switch(&idle.ctx, &B.ctx)
  │    └─ 恢复 B 的寄存器 → 跳转到 B.PC
  │
  任务 B (运行中)
```

**关键**：调度循环不是独立任务，而是在 idle 上下文中运行。idle 和用户任务通过 `kern_ctx_switch` 轮流共享同一个 CPU，无需信号量。

---

## 3. Per-CPU 就绪队列 (`kern_runq`)

### 3.1 数据结构

```c
#define KERN_PRIO_MAX     32       // 0=最高优先级，31=最低（idle 专用）
#define KERN_PRIO_IDLE    31
#define KERN_PRIO_DEFAULT 16
#define KERN_TIME_SLICE_MS 10      // 默认时间片

typedef struct kern_runq {
    kern_task_t *queues[KERN_PRIO_MAX];       // 每优先级 FIFO 头
    kern_task_t *queue_tails[KERN_PRIO_MAX];  // 每优先级 FIFO 尾（O(1) 追加）
    uint32_t     bitmap;                      // bit[i]=1 表示优先级 i 有任务
    uint8_t      count;                       // 队列中任务总数
} kern_runq_t;
```

### 3.2 核心操作（全部 O(1)）

```
enqueue(task):
    prio = task->priority
    bitmap |= (1 << prio)
    if (queues[prio] == NULL)
        queues[prio] = task
    else
        queue_tails[prio]->runq_next = task
    queue_tails[prio] = task
    count++

pick_next():
    if (bitmap == 0) return NULL
    prio = __builtin_ctz(bitmap)         // 最高优先级（count trailing zeros）
    task = queues[prio]
    queues[prio] = task->runq_next
    if (queues[prio] == NULL)
        bitmap &= ~(1 << prio)           // 该优先级队列已空
    count--
    return task
```

---

## 4. 调度器核心 (`kern_sched`)

### 4.1 每核调度循环

```c
void kern_sched_loop(void *arg) {
    uint8_t cpu = (uint8_t)(uintptr_t)arg;

    for (;;) {
        // 1. 回收僵尸任务资源
        reap_zombies(cpu);

        // 2. 唤醒到期睡眠任务
        kern_sleep_tick(cpu);

        // 3. 选择下一个就绪任务
        kern_task_t *next = kern_runq_pick_next(&g_per_cpu[cpu].runq);

        if (next != NULL) {
            g_per_cpu[cpu].current_task = next;
            next->state = KERN_TASK_RUNNING;
            next->slice_remaining = KERN_TIME_SLICE_MS;

            // 上下文切换到任务 — 返回值从这里继续
            kern_ctx_switch(&g_per_cpu[cpu].idle_ctx, &next->ctx);
            // ─── 任务 yield/exit/被抢占后回到这里 ───
        } else {
            // 无就绪任务：等待中断唤醒
            kern_cpu_idle(cpu);
        }
    }
}
```

### 4.2 调度策略

| 事件 | 动作 |
|------|------|
| 任务创建（高优先级） | 入 runqueue，若优先级 > current 则标记 need_resched |
| 中断唤醒高优先级任务 | ISR 返回时标记 need_resched，直接调度 |
| mutex 释放有等待者 | 唤醒等待者，若其优先级更高则 yield |
| 时间片耗尽 | 当前任务重新入队到同优先级队尾，选择下一个 |
| 任务主动 yield() | kern_ctx_switch 回调度循环，重新入队 |
| 任务 sleep_ms() | 标记 SLEEPING，加入睡眠链表，yield |

---

## 5. 同步原语 (`kern_sync`)

### 5.1 Spinlock

```c
typedef struct { volatile uint32_t lock; } spinlock_t;

void spin_lock(spinlock_t *s) {
    while (__sync_lock_test_and_set(&s->lock, 1))
        while (s->lock) {}   // 读时不自旋，减少总线压力
    __sync_synchronize();
}

void spin_unlock(spinlock_t *s) {
    __sync_synchronize();
    __sync_lock_release(&s->lock);
}
```

约束：持有 spinlock 期间禁止抢占（need_resched 被延迟），临界区 < 10μs。单核模式退化空操作。

### 5.2 Mutex（递归锁 + 优先级继承）

```c
typedef struct {
    spinlock_t   guard;
    kern_task_t *owner;
    uint8_t      recurse;
    kern_task_t *waiters;
    uint8_t      orig_prio;  // 原始优先级（用于继承恢复）
} mutex_t;
```

acquire 流程：
1. 锁空闲 → 设 owner=current, orig_prio=current.prio, recurse=1
2. owner==current → recurse++（递归获取）
3. 已被他人持有 → current→BLOCKED，加入 waiters，若 current.prio < owner.orig_prio 则提升 owner 优先级（优先级继承），kern_yield()

release 流程：
1. recurse > 1 → recurse--
2. recurse == 1 → 恢复 owner.prio=orig_prio，唤醒首个 waiter → READY → 入 runqueue
3. 若唤醒的 waiter 优先级高于当前，yield

### 5.3 Semaphore（计数信号量）

```c
typedef struct {
    spinlock_t   guard;
    int32_t      count;
    uint32_t     max_count;
    kern_task_t *waiters;
} sem_t;
```

- wait: count > 0 则 count--，否则 BLOCKED → 加入 waiters → yield
- post: waiters 非空则唤醒首个，否则 count < max 则 count++

### 5.4 Condition Variable

与 mutex 配合使用。cond_wait 原子释放 mutex + 进入等待 + 被唤醒后自动重获 mutex。

---

## 6. 定时器系统 (`kern_timer`)

### 6.1 硬件定时器：CCOMPARE0

使用 Xtensa 核内 CCOMPARE0 定时器，不使用 ESP-IDF GPTimer 外设驱动。

```
频率：1kHz (1ms tick)
每核独立 CCOUNT/CCOMPARE 寄存器
中断号：ETS_CCOMPARE0_INUM（level 1，低优先级）
```

### 6.2 ISR 流程

```
kern_timer_isr(cpu):
    1. 重装载 CCOMPARE0 = CCOUNT + (cpu_freq / 1000)
    2. g_per_cpu[cpu].sched_ticks++
    3. if (current && --current->slice_remaining == 0)
         g_per_cpu[cpu].need_resched = true
    4. kern_soft_timer_tick()（仅 Core 0）
    5. ISR 返回：
       if (need_resched && !嵌套中断)
           kern_ctx_switch_isr(current.ctx, idle_ctx)
           → 进入调度循环重选任务
```

### 6.3 软件定时器

按到期时间排序的单向链表，每 tick O(1) 检查头部。

```c
typedef struct kern_soft_timer {
    uint64_t    expire_tick;
    void      (*callback)(void *arg);
    void       *arg;
    bool        periodic;
    uint32_t    period_ticks;
    struct kern_soft_timer *next;
} kern_soft_timer_t;
```

---

## 7. 双核 SMP (`kern_smp`)

### 7.1 Per-CPU 数据结构

```c
typedef struct kern_per_cpu {
    uint8_t      cpu_id;
    kern_task_t *current_task;
    kern_task_t *idle_task;
    kern_runq_t  runq;
    kern_ctx_t   idle_ctx;
    uint64_t     sched_ticks;
    volatile bool need_resched;
    volatile bool ipi_pending;
    uint8_t      task_count;
    uint8_t      _pad[2];       // cache line 对齐
} kern_per_cpu_t;

extern kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];
```

访问宏：
```c
#define KERN_THIS_CPU   kern_cpu_id()        // 通过 PRID 寄存器获取
#define g_current_task  (g_per_cpu[KERN_THIS_CPU].current_task)
#define g_need_resched  (g_per_cpu[KERN_THIS_CPU].need_resched)
```

### 7.2 启动序列

- Core 0: `app_main()` → `kern_init()` → 创建 Core 0 idle → 启动 CCOMPARE0 → `esp_rom_aps_digi_boot()` 释放 Core 1 → 进入 `kern_sched_loop(0)`
- Core 1: ROM 引导 → `kern_smp_entry_core1()` → 初始化 Per-CPU 数据 → 创建 Core 1 idle → 启动自己的 CCOMPARE0 → `kern_sched_loop(1)`

两个核心的调度循环完全独立并行运行。

### 7.3 核间中断 IPI

使用 Xtensa 软件中断（`wsr.intset` 写 Core 0/1 对应的中断位）：

- 场景 1：`kern_task_kill()` 目标在另一核 → 标记 ZOMBIE + 发 IPI 强制重调度
- 场景 2：跨核入队 → 入队后目标核空闲则发 IPI 唤醒
- 场景 3：任务迁移 → dequeue 当前核 → enqueue 目标核 → 发 IPI

### 7.4 任务亲和性

```
kern_task_t 新增字段：
  uint8_t cpu_id;          // 当前运行核心（KERN_CPU_ANY=0xFF 自动分配）
  uint8_t affinity_mask;   // 允许运行的核位掩码（bit0=Core0, bit1=Core1）
```

创建任务时默认 affinity_mask=0x03（双核均可），cpu_id 由负载均衡选择任务数较少的核。

---

## 8. 睡眠/唤醒 (`kern_sleep`)

### 8.1 API

```c
void kern_sleep_ms(uint32_t ms);  // 替代所有 vTaskDelay
```

### 8.2 实现

- 睡眠链表按 `wake_tick`（绝对 tick 数）排序
- 调用时：任务状态 → SLEEPING，插入链表，kern_yield()
- 每 tick：`kern_sleep_tick()` 检查链表头部 → 到期的任务移回就绪队列

---

## 9. API 迁移表

| FreeRTOS API | 替换 | 影响文件 |
|-------------|------|---------|
| `vTaskDelay(ms)` | `kern_sleep_ms(ms)` | main.cpp, hal_system.cpp, ui_task.c, wifi_manager.cpp, tu_api.cpp |
| `xTaskCreatePinnedToCore` | `kern_spawn` (内部) | kern_port_freertos.c → 移除 |
| `vTaskDelete` | `kern_task_kill` (内部) | kern_port_freertos.c → 移除 |
| `xSemaphore*` | `kern_sync` sem 系列 | kern_port_freertos.c → 移除 |
| `esp_timer_get_time()` | 保留（不依赖 FreeRTOS） | hal_system.cpp |
| `heap_caps_*` | 保留（不依赖 FreeRTOS） | main.cpp, kern_kmalloc.c |
| `nvs_flash_*` | 保留（不依赖 FreeRTOS） | main.cpp |
| `gpio_*` | 保留（不依赖 FreeRTOS） | kern_gpiofs.c, hal_input.cpp |

---

## 10. 文件改动清单

### 新增文件（8 个）

| 文件 | 职责 |
|------|------|
| `src/kernel/kern_ctx.h` | 上下文结构 kern_ctx_t 定义 |
| `src/kernel/kern_ctx_esp32.S` | Xtensa 汇编上下文切换 |
| `src/kernel/kern_runq.h` | Per-CPU 就绪队列接口 |
| `src/kernel/kern_runq.c` | Bitmap + FIFO 队列实现 |
| `src/kernel/kern_timer.c` | CCOMPARE0 ISR + 软件定时器 |
| `src/kernel/kern_sleep.c` | 睡眠/唤醒链表 |
| `src/kernel/kern_cpu.h` | Per-CPU 数据结构 + IPI |
| `src/kernel/kern_intr.S` | 中断入口/出口 trampoline |

### 重写文件（4 个）

| 文件 | 改动 |
|------|------|
| `src/kernel/kern_sched.c` | 新调度循环，合并 kern_sched_loop |
| `src/kernel/kern_smp.c` | 双核启动 + IPI + 亲和性 |
| `src/kernel/kern_port_freertos.c` | → 改为 `kern_port_bare.c`，全新后端 |
| `src/kernel/kern_sync.c` | Mutex/sem 等待走 kern_yield 路由 |

### 修改文件（9 个）

| 文件 | 改动 |
|------|------|
| `src/kernel/kern_task.c` | 新增 TCB 字段（ctx, runq_link, sleep_next, affinity 等） |
| `src/kernel/kern_types.h` | 新增优先级、亲和性类型定义 |
| `src/kernel/kern_init.c` | 新启动序列 |
| `src/main.cpp` | 移除 FreeRTOS include，vTaskDelay → kern_sleep_ms |
| `src/hal/hal_system.cpp` | vTaskDelay → kern_sleep_ms |
| `src/app/ui_task.c` | vTaskDelay → kern_sleep_ms |
| `src/app/wifi/wifi_manager.cpp` | vTaskDelay → kern_sleep_ms |
| `src/app/token_usage/tu_api.cpp` | vTaskDelay → kern_sleep_ms |
| `src/CMakeLists.txt` | 添加新 .c/.S 文件，移除 FreeRTOS 链接 |

### 配置修改

| 文件 | 改动 |
|------|------|
| `sdkconfig.defaults` | 关闭 FreeRTOS 相关选项：CONFIG_FREERTOS_UNICORE、CONFIG_FREERTOS_HZ 等 |

---

## 11. 风险与缓解

| 风险 | 缓解 |
|------|------|
| ESP-IDF WiFi/BLE 内部深度依赖 FreeRTOS 任务/队列 | 短期实现 FreeRTOS shim 兼容层提供桩 API；长期替换或放弃 |
| Xtensa 汇编上下文切换正确性 | Native test 先行验证逻辑，真机分步启用 |
| 优先级反转导致死锁 | Mutex 实现优先级继承 |
| CCOMPARE0 中断与 ESP-IDF 外设中断冲突 | CCOMPARE0 用 level 1（低优先级），不抢占关键外设 ISR |
| Native test 无法覆盖硬件相关代码 | 真机 CI 测试 + 串口日志验证 |

---

## 12. 实施策略

按依赖关系分五个阶段实施：

| 阶段 | 内容 | 可验证 |
|------|------|--------|
| **Phase 1** | kern_ctx + kern_intr 上下文切换基础设施 | Native test: 上下文保存/恢复/init 正确 |
| **Phase 2** | kern_runq + kern_sched 调度器 | 单核运行，任务创建/yield/抢占通过 |
| **Phase 3** | kern_timer + kern_sleep | 定时器准确，sleep_ms 唤醒正确 |
| **Phase 4** | kern_sync + API 迁移 | 所有 vTaskDelay 替换，同步原语测试通过 |
| **Phase 5** | kern_smp 双核 | Core 1 启动，IPI，任务亲和性，双核并发测试 |

每个阶段先在 Native test 验证，再部署到 M5Stick-C 真机。
