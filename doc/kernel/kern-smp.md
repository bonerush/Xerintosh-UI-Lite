# SMP 多核支持（Kern SMP）

> **Parent:** [内核总览](index.md) | **Related:** [同步原语](kern-sync.md), [调度器](kern-task.md), [类型系统](kern-types.md)

## 概述

`kern_smp.h/c` 提供 Xeros 内核的**对称多处理器（SMP）**支持。ESP32 有两个 Xtensa LX6 核心（PRO_CPU=0, APP_CPU=1），SMP 子系统让 Xeros 可以同时在两个核心上运行调度器，实现真正的并行任务执行。

**核心设计原则：零开销退化。** 当 `CONFIG_SMP_ENABLED` 未定义时，所有 per-CPU 访问退化为 `g_per_cpu[0]` 的直接访问，经编译器优化后与普通全局变量无差别。

---

## 关键概念

### Per-CPU 数据结构

*📄 Source: [kern_smp.h](../../src/kernel/kern_smp.h#L29-L39)*

```c
#define KERN_MAX_CPUS  2     /* ESP32: PRO_CPU=0, APP_CPU=1 */
#define KERN_CPU_ANY   0xFF  /* 自动分配 */

typedef struct kern_per_cpu {
    uint8_t       cpu_id;           /* CPU 编号 */
    struct kern_task  *current_task;     /* 当前运行的任务 */
    struct kern_task  *idle_task;        /* 此 CPU 的 idle 任务 */
    uint32_t      sched_ticks;      /* 调度 tick 计数 */
    volatile bool need_resched;     /* 抢占请求标志（可被 ISR 设置） */
} kern_per_cpu_t;

/** 全局 per-CPU 数组（总是存在，SMP 禁用时仅使用 [0]） */
extern kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];
```

#### Per-CPU 数组结构图

```
g_per_cpu[KERN_MAX_CPUS]

 ┌─────────────────────────────────────────────────────────────┐
 │ g_per_cpu[0]              │ g_per_cpu[1]                    │
 │ ┌───────────────────────┐ │ ┌───────────────────────┐       │
 │ │ cpu_id: 0             │ │ │ cpu_id: 1             │       │
 │ │ current_task: → shell │ │ │ current_task: → wifi  │       │
 │ │ idle_task: → idle0    │ │ │ idle_task: → idle1    │       │
 │ │ sched_ticks: 123456   │ │ │ sched_ticks: 123455   │       │
 │ │ need_resched: false   │ │ │ need_resched: true    │       │
 │ └───────────────────────┘ │ └───────────────────────┘       │
 │       PRO_CPU             │         APP_CPU                  │
 └─────────────────────────────────────────────────────────────┘
```

每个 CPU 拥有独立的 `current_task`、`idle_task`、`sched_ticks` 和 `need_resched`。这意味着两个核心可以**同时执行不同的 Xeros 任务**而互不干扰。

### Per-CPU 访问宏：零开销的关键

*📄 Source: [kern_smp.h](../../src/kernel/kern_smp.h#L67-L75)*

```c
#define g_current_task   (g_per_cpu[KERN_THIS_CPU].current_task)
#define g_idle_task      (g_per_cpu[KERN_THIS_CPU].idle_task)
#define g_sched_ticks    (g_per_cpu[KERN_THIS_CPU].sched_ticks)

#ifdef CONFIG_PREEMPT_ENABLED
#define g_need_resched   (g_per_cpu[KERN_THIS_CPU].need_resched)
#endif
```

#### 宏展开原理

```
SMP 模式 (CONFIG_SMP_ENABLED):

    KERN_THIS_CPU → kern_cpu_id() → xPortGetCoreID()（硬件寄存器读取）
    g_current_task → g_per_cpu[kern_cpu_id()].current_task
                    → 间接寻址（通过 volatile 变量索引数组）

    开销: 一次 volatile 读取 + 一次数组偏移计算


单核模式 (!CONFIG_SMP_ENABLED):

    KERN_THIS_CPU → ((uint8_t)0)  ← 编译器常量
    g_current_task → g_per_cpu[0].current_task
                    → 直接寻址（编译器优化为绝对地址）

    开销: 与普通全局变量完全等价，零额外指令
```

#### 中文伪代码拆解

```
/* 零开销退化的秘密：
 *
 * 当 CONFIG_SMP_ENABLED 未定义时:
 *   KERN_THIS_CPU = ((uint8_t)0)  ← 编译期常量
 *   g_per_cpu[0]                  ← 常数索引
 *
 * 编译器在优化阶段:
 *   1. 识别 KERN_THIS_CPU 恒为 0
 *   2. 将 g_per_cpu[0] 替换为直接地址
 *   3. 消除数组索引指令
 *
 * 结果: g_current_task 变成与普通全局变量
 *       完全相同的单条 LOAD 指令
 */
```

### CONFIG_SMP_ENABLED 两种模式对比

*📄 Source: [kern_smp.h](../../src/kernel/kern_smp.h#L43-L65)*

```c
#ifdef CONFIG_SMP_ENABLED
/* ============ SMP 模式 ============ */

#define KERN_THIS_CPU  ((uint8_t)(kern_cpu_id()))

uint8_t kern_cpu_id(void);            /* xPortGetCoreID() */
void kern_smp_init(void);             /* 初始化 per-CPU 数组，启动 APP_CPU */
void kern_smp_start_core(uint8_t, void (*entry)(void *arg));

#else
/* ============ 单核模式 ============ */

#define KERN_THIS_CPU  ((uint8_t)0)

static inline uint8_t kern_cpu_id(void) { return 0; }
#define kern_smp_init()                 do {} while (0)
#define kern_smp_start_core(c, e)       do { (void)(c); (void)(e); } while (0)
#endif
```

| 特性 | SMP 模式 (`CONFIG_SMP_ENABLED`) | 单核模式 (未定义) |
|------|------|------|
| `KERN_THIS_CPU` | `kern_cpu_id()` → `xPortGetCoreID()`（硬件 ID） | `0`（编译期常量） |
| `kern_cpu_id()` | `xPortGetCoreID()` → 实际 CPU 号 | `return 0`（内联为常量） |
| `kern_smp_init()` | 遍历初始化 `g_per_cpu[0..1]`，启动 APP_CPU | `do {} while(0)`（空操作） |
| `kern_smp_start_core()` | `xTaskCreatePinnedToCore()` 创建 FreeRTOS 任务 | 空操作（抑制参数未使用警告） |
| 代码体积 | +~2KB | 0 bytes |
| 运行时开销 | per-CPU 间接寻址 | 直接寻址 |

### SMP 初始化流程

*📄 Source: [kern_smp.c](../../src/kernel/kern_smp.c#L34-L44)*

```c
void kern_smp_init(void)
{
    for (uint8_t i = 0; i < KERN_MAX_CPUS; i++) {
        g_per_cpu[i].cpu_id       = i;
        g_per_cpu[i].current_task = NULL;
        g_per_cpu[i].idle_task    = NULL;
        g_per_cpu[i].sched_ticks  = 0;
        g_per_cpu[i].need_resched = false;
    }
    kern_log(KERN_LOG_INFO, "SMP: %d CPUs initialized", KERN_MAX_CPUS);
}
```

#### 中文伪代码拆解

```
函数 SMP初始化() {
    遍历 i = 0 到 (核心数-1):
        per-CPU[i].CPU编号 = i
        per-CPU[i].当前任务 = 空
        per-CPU[i].空闲任务 = 空
        per-CPU[i].调度tick = 0
        per-CPU[i].需重调度 = 否

    输出日志 "SMP: 2 CPUs initialized"
}
```

### 启动辅助核心

*📄 Source: [kern_smp.c](../../src/kernel/kern_smp.c#L48-L67)*

```c
void kern_smp_start_core(uint8_t cpu_id, void (*entry)(void *arg))
{
    if (cpu_id >= KERN_MAX_CPUS) return;

    BaseType_t ret = xTaskCreatePinnedToCore(
        (TaskFunction_t)entry,
        "xeros_smp",
        4096,                   /* 4KB 栈 */
        NULL,
        tskIDLE_PRIORITY + 2,   /* 比 idle 高两级 */
        NULL,
        cpu_id                  /* 固定到指定核心 */
    );

    if (ret != pdPASS) {
        kern_log(KERN_LOG_WARN, "SMP: failed to start core %d scheduler", cpu_id);
    }
}
```

#### 中文伪代码拆解

```
函数 启动辅助核心(CPU编号, 入口函数) {
    if (CPU编号 >= 最大核心数) 返回

    在指定CPU上创建FreeRTOS任务:
        入口函数 = 参数传入的调度循环入口
        任务名 = "xeros_smp"
        栈大小 = 4KB
        优先级 = idle+2（比idle高但比大多数用户任务低）
        钉住CPU = CPU编号

    if (创建失败) {
        输出警告 "SMP: failed to start core N scheduler"
    } else {
        输出日志 "SMP: core N scheduler started"
    }
}

/* 核心思路：
 * - 每个 CPU 上创建一个独立的 FreeRTOS 任务运行 Xeros 调度循环
 * - 使用 xTaskCreatePinnedToCore 将任务绑在指定核心
 * - 两个核心的 Xeros 调度循环各自独立运行，共享 g_task_list
 */
```

### 任务-CPU 亲和性

*📄 Source: [kern_task.h](../../src/kernel/kern_task.h#L76)*

```c
uint8_t cpu_id;  /* 绑定的 CPU（KERN_CPU_ANY 表示自动分配） */
```

```
亲和性工作流：

kern_spawn("wifi", entry, NULL, 0)
    │
    ├─ cpu_id == KERN_CPU_ANY (0xFF)
    │   → 分配策略：
    │      - 单核模式：始终分配到 CPU 0
    │      - SMP 模式：分配到 kern_cpu_id()（当前 CPU）
    │
    └─ cpu_id == 1 (APP_CPU)
        → 任务始终运行在 CPU 1 上
        → 调度器在 tick 中检查：如果当前任务绑定了其他 CPU，跳过
```

---

## 与其他组件的关系

- **kern_sync**：spinlock 和 mutex 依赖 `KERN_THIS_CPU` 和 `g_current_task` per-CPU 宏实现正确的所有者跟踪
- **kern_sched**：调度器使用 `g_current_task`（per-CPU 宏）而不是直接的全局变量。`kern_sched_init()` 调用 `kern_smp_init()`
- **kern_task**：TCB 的 `cpu_id` 字段决定任务运行在哪个 CPU
- **kern_mpu**：MPU 上下文切换时使用 `KERN_THIS_CPU` 确定当前核心

---

> **See Also:** [同步原语](kern-sync.md) | [调度器](kern-task.md) | [可插拔调度类](kern-sched-class.md) | [类型系统](kern-types.md)
