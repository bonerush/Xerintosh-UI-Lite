# 抢占式调度器（Kern Task）

> **Parent:** [内核总览](index.md) | **Related:** [类型系统](kern-types.md), [可插拔调度类](kern-sched-class.md), [SMP 支持](kern-smp.md), [MPU 保护](kern-mpu.md), [资源追踪](kern-resource.md)

## 概述

`kern_task` 定义了 Xeros 内核的**任务控制块（TCB）**和任务生命周期管理。v2 中 TCB 新增了 `cpu_id`（SMP 亲和性）、`resource_head`（资源追踪）、`mpu_config`（MPU 配置）、`priority`（优先级）、`timeslice_remaining`（时间片）和 `scheduler_class_id`（调度类索引）字段，支持 SMP 双核调度、抢占式调度类和内存保护。

核心操作：
- `kern_spawn(name, entry, arg, stack_min)` — 创建并启动新任务
- `kern_yield()` — 主动让出 CPU（触发重调度）
- `kern_sleep_ms(ms)` — 进入睡眠，ms 毫秒后自动唤醒
- `kern_exit()` — 退出当前任务，自动释放所有资源
- `kern_task_kill(pid)` — 从外部终止指定任务

---

## 关键概念

### 任务控制块（TCB）

*📄 Source: [kern_task.h](../../src/kernel/kern_task.h#L44-L86)*

```c
typedef struct kern_task {
    kern_pid_t          pid;                    /* 任务 ID */
    char                name[KERN_TASK_NAME_LEN + 1];  /* 任务名称 */
    kern_task_state_t   state;                  /* 当前状态 */

    /* 动态栈管理 */
    uint8_t            *stack_base;             /* 栈底指针 */
    size_t              stack_size;             /* 栈大小（字节） */

    /* 上下文保存（平台相关） */
#if defined(NATIVE_TEST)
    kern_ctx_t          ctx;                    /* ucontext */
#elif defined(XEROS_NATIVE_SCHED)
    kern_ctx_t          ctx;                    /* setjmp/longjmp */
#else
    kern_port_thread_t  port_thread;            /* FreeRTOS 线程句柄 */
#endif

    /* 调度信息（v2 新增） */
    uint32_t            wake_time;              /* 唤醒时间戳（sleep 用） */
    uint8_t             priority;               /* 优先级（0=最低，255=最高） */
    uint8_t             timeslice_remaining;     /* 当前时间片剩余 tick */
    int8_t              scheduler_class_id;     /* 所属调度类索引（-1=未分配） */

    /* 入口 */
    void              (*entry)(void *arg);      /* 任务主函数 */
    void               *arg;                    /* 入口参数 */

    /* 标志位 */
    uint8_t             flags;                  /* KERN_TASK_FLAG_* */

    /* SMP 亲和性（v2 新增） */
    uint8_t             cpu_id;                 /* 绑定 CPU（KERN_CPU_ANY=自动） */

    /* 链表指针 */
    struct kern_task   *next;                   /* 下一个任务 */

    /* 资源追踪（v2 新增） */
    struct kern_resource *resource_head;        /* 持有的资源链表头 */

    /* MPU 内存保护（v2 新增） */
    kern_mpu_config_t  *mpu_config;             /* 每任务 MPU 配置（可为 NULL） */
} kern_task_t;
```

#### 中文伪代码拆解

```
结构体 任务控制块 {
    /* 基础身份 */
    进程ID           整数（int16_t，非负）
    任务名称[17字节] 字符串（最多15个可见字符）
    当前状态         枚举 {就绪/运行/睡眠/阻塞/僵尸}

    /* 动态栈 */
    栈底指针        指向栈的最低地址
    栈大小           当前分配的字节数
    上下文对象       平台相关（ucontext / setjmp_buf / FreeRTOS句柄）

    /* ___v2新增___ 调度元数据 */
    唤醒时间戳        毫秒级，用于睡眠到期检查
    优先级            0-255，FIFO 调度类用
    时间片剩余        tick数，RR 调度类用完则触发抢占
    调度类索引        指向 g_sched_classes[]，-1 表示未分配

    /* 入口信息 */
    入口函数指针      任务启动后调用的第一个函数
    入口参数          传给入口函数的 void* 参数

    /* ___v2新增___ 扩展字段 */
    标志位            位掩码（bit0=虚拟任务）
    CPU编号          0=PRO_CPU, 1=APP_CPU, 0xFF=自动分配
    资源链表头        指向内核分配器、文件句柄等资源链表
    MPU配置指针       NULL=无保护，非NULL=独立MPU区域配置
}
```

### v2 新增字段详解

| 字段 | 类型 | 默认值 | 用途 |
|------|------|--------|------|
| `cpu_id` | `uint8_t` | `KERN_CPU_ANY` (0xFF) | SMP 任务亲和性：指定运行在哪个 CPU，`KERN_CPU_ANY` 由调度器自动分配 |
| `resource_head` | `kern_resource_t*` | `NULL` | 资源追踪链表头。`kern_kmalloc()` 等内核分配函数自动将分配挂载到此链表。`kern_exit()` 时自动释放 |
| `mpu_config` | `kern_mpu_config_t*` | `NULL` | 每任务的 MPU 区域配置。上下文切换时 `kern_mpu_apply()` 加载此配置 |
| `priority` | `uint8_t` | `128`（`kern_spawn` 中硬编码） | 优先级 0-255。RR 不使用此字段，FIFO 按优先级排序 |
| `timeslice_remaining` | `uint8_t` | `SCHED_RR_DEFAULT_TIMESLICE` (10) | RR 时间片剩余 tick。每次 `sched_rr_tick()` 递减，归零时标记 `g_need_resched` |
| `scheduler_class_id` | `int8_t` | `0`（`calloc` 默认，未显式初始化） | 指向 `g_sched_classes[]` 索引。当前代码中未在 `kern_spawn()` 中显式设置，依赖 `calloc` 清零 |

### 调度循环与 pick_next_ready

*📄 Source: [kern_sched.c](../../src/kernel/kern_sched.c#L220-L313)*

调度 tick 的核心流程已在 [index.md](index.md#调度流程总览) 中图示。简言之，每个 tick 执行：

```
kern_sched_tick():
    ├─ tick 计数++
    ├─ 回收僵尸任务
    ├─ 遍历 class->tick()（RR 时间片递减、FIFO 抢占检测）
    ├─ 检查 need_resched（时间片用尽或更高优先级任务就绪）
    └─ pick_next_ready() → 上下文切换
```

其中 `pick_next_ready()` 遍历 `g_sched_classes[]` 数组，调用每个 class 的 `pick_next()` 回调。详见 [可插拔调度类](kern-sched-class.md)。

### 上下文保存与恢复

```
ESP32（FreeRTOS 令牌协议）：
    kern_port 层为每个 Xeros 任务创建一个 FreeRTOS 任务
    上下文切换 = 释放二值信号量令牌给下一个任务 → 当前任务等待令牌

Native（POSIX ucontext）：
    使用 swapcontext() 完整寄存器保存/恢复
    每个任务独立的栈 + 通用寄存器上下文

XEROS_NATIVE_SCHED（setjmp/longjmp）：
    使用 setjmp 保存 / longjmp 恢复
    栈由协程框架管理
```

**为什么三个平台用不同策略？**

| 策略 | ESP32 | Native | XEROS_NATIVE_SCHED |
|------|-------|--------|---------------------|
| 机制 | FreeRTOS 二值信号量令牌 | `swapcontext` (POSIX) | `setjmp/longjmp` |
| 原因 | ESP32 的 FreeRTOS 上下文切换可线程安全 | 桌面环境 `ucontext` 更完整且易调试 | 无 ucontext 的 ESP32 本地调试 |
| 切换开销 | 中等（FreeRTOS 调度） | 中等（寄存器 + 信号掩码） | 低（仅寄存器快照） |

### 动态栈管理

*📄 Source: [kern_sched.c](../../src/kernel/kern_sched.c#L234-L243)（Native 测试环境）*

```c
/* tick 中每 500 tick 检查一次栈使用率（仅 Native 后端） */
if (g_current_task != NULL && g_current_task != g_idle_task
    && (g_sched_ticks % 500) == 0) {
    size_t usage = kern_task_stack_usage(g_current_task);
    if (usage > 0 && g_current_task->stack_size > 0
        && usage > g_current_task->stack_size * 3 / 4) {
        kern_log(KERN_LOG_WARN,
                 "task %s stack usage %zu/%zu (>75%%)",
                 g_current_task->name, usage, g_current_task->stack_size);
    }
}
```

> **注意**：ESP32 FreeRTOS 后端中栈由 FreeRTOS 自动管理，`kern_task_stack_usage()` 通过 `uxTaskGetStackHighWaterMark()` 查询（见 [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L279-L287)），tick 中不做主动扫描。

#### 中文伪代码拆解

```
函数 栈检查(当前任务) {
    每500个tick执行一次:
        计算已用栈空间 = kern_task_stack_usage(任务)
        if (使用率超过75%) {
            输出警告日志 "task xxx stack usage 800/1024 (>75%)"
            /* 不自动扩展，仅警告。开发者应调整 stack_min 参数 */
        }
}
```

**栈安全防线（Native 后端）：**

| 层级 | 检测机制 | 触发条件 | 行为 |
|------|----------|----------|------|
| 1 | 金丝雀 | `canary != 0xDEADC0DE` | 输出警告日志（见 [kern_task_stack.c](../../src/kernel/kern_task_stack.c#L67-L92)） |
| 2 | SP 使用率警告 | `usage > stack_size * 75%` | 输出警告日志（仅 Native tick 中检查） |
| 3 | 上限限制 | `stack_size > KERN_STACK_MAX (8KB)` | `task_stack_init()` 截断到 8KB |

> **注意**：ESP32 FreeRTOS 后端中栈由 FreeRTOS 自动管理，金丝雀和栈扫描机制不适用。`task_stack_init()` 和 `task_write_canary()` 在此后端中为空操作（见 [kern_task_stack.c](../../src/kernel/kern_task_stack.c#L48-L60)）。`kern_task_stack_usage()` 通过 `uxTaskGetStackHighWaterMark()` 查询（见 [kern_port_freertos.c](../../src/kernel/kern_port_freertos.c#L279-L287)）。

### 任务生命周期

```
任务生命周期（v2）：

1. SPAWN 阶段
   └→ kern_spawn("ui", ui_task_main, NULL, 4096)
       ├→ 分配 TCB（calloc，所有字段默认清零）
       ├→ 设置 priority = 128（默认值）
       ├→ 设置 timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE
       ├→ 设置 cpu_id = KERN_CPU_ANY（自动分配；SMP 模式下调用 kern_smp_migrate_assign()）
       ├→ PID 递增分配
       ├→ 创建底层上下文（FreeRTOS线程 / ucontext / setjmp栈）
       ├→ 插入 g_task_list 链表尾部
       ├→ 调用 kern_mpu_setup_stack_guard() 配置栈守卫（Native 后端）
       └→ 状态设为 READY

   *📄 Source: [kern_task_lifecycle.c](../../src/kernel/kern_task_lifecycle.c#L159-L232)（ESP32 FreeRTOS 后端）*

2. RUNNING 阶段
   └→ pick_next_ready() 选中该任务后
       ├→ 状态设为 RUNNING
       ├→ kern_mpu_apply(task)  ← v2 新增：加载 MPU 区域
       └→ 上下文切换（swapcontext / 二值信号量令牌 / longjmp）

3. YIELD 阶段
   └→ 任务调用 kern_yield()
       ├→ 状态保持 READY（非 RUNNING → 调度器不会再次选中）
       └→ 上下文切换回调度器

4. SLEEP 阶段
   └→ 任务调用 kern_sleep_ms(500)
       ├→ 状态设为 SLEEPING
       ├→ wake_time = g_sched_ticks + 500
       └→ 在 sched_rr_pick_next() 第一遍扫描中唤醒到期任务

5. EXIT 阶段
   └→ 任务调用 kern_exit()
       ├→ kern_resource_release_all(cur)  ← v2 新增：自动释放当前任务的所有追踪资源
       ├→ 状态设为 ZOMBIE
       └→ reap_zombies() 在下次 tick 中释放 TCB 和栈内存
```

### 空闲任务（Idle Task）

调度器初始化时自动创建 `[idle]` 任务：
- **优先级最低**（`priority = 0`）
- **时间片充足**（`timeslice_remaining = 10`）
- **永不睡眠**，循环调用 `kern_yield()`
- 当没有就绪任务时，idle 是所有 class 的兜底选择

SMP 模式下，每个 CPU 有独立的 idle 任务（通过 `g_per_cpu[cpu].idle_task` 引用）。

### 虚任务（Virtual Task）

虚任务是为 `user_item` App 提供内核可观测性的轻量机制。

特点：
- **无独立上下文**：不创建 FreeRTOS 任务/线程，不参与调度
- **仅用于可观测性**：`/proc/tasks` 可见、`kill` 可终止、任务管理器可管理
- **生命周期绑定 App**：进入 user_item 时 `kern_task_register_virtual()`，退出时 `kern_task_unregister_virtual()`
- **终止行为**：被 kill 后通过 `g_xerintosh_exit_requested` 标志通知 UI 任务退出当前 App

```c
// 注册虚任务
kern_pid_t pid = kern_task_register_virtual("serial_monitor");

// 注销虚任务
kern_task_unregister_virtual(pid);
```

虚任务在 `/proc/tasks` 中显示为 `0/0` 栈使用（因 `stack_size=0`，见 [kern_procfs.c](../../src/kernel/kern_procfs.c#L81-L101)），`flags` 字段不在输出中。

---

## 与其他组件的关系

- **kern_sched_class**：`scheduler_class_id` 字段预留用于指向所属调度类，但当前 `kern_spawn()` 中未显式初始化（依赖 `calloc` 清零为 0）。任务直接插入全局 `g_task_list` 链表，未通过 `class->enqueue()` 注册
- **kern_smp**：`cpu_id` 决定任务运行在哪个 CPU，`KERN_CPU_ANY` 表示由 `kern_smp_migrate_assign()` 自动分配（SMP 模式下分配到任务数较少的核心）
- **kern_mpu**：`mpu_config` 在上下文切换时由 `kern_mpu_apply()` 加载（当前为框架实现，尚未写入实际硬件寄存器）
- **kern_resource**：`resource_head` 在任务退出时自动遍历释放
- **kern_vfs**：VFS 使用全局 `g_fd_table[]` 管理文件描述符，任务退出时由 `kern_resource_release_all()` 自动关闭关联的 FD
- **kern_procfs**：`/proc/tasks` 遍历调度器链表显示任务信息

---

> **See Also:** [类型系统](kern-types.md) | [可插拔调度类](kern-sched-class.md) | [Round-Robin 类](kern-sched-rr.md) | [FIFO 类](kern-sched-fifo.md) | [SMP 支持](kern-smp.md)
