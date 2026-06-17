# 内核类型系统（Kern Types）

> **Parent:** [内核总览](index.md) | **Related:** [调度器](kern-task.md), [SMP 支持](kern-smp.md), [同步原语](kern-sync.md), [可插拔调度类](kern-sched-class.md)

## 概述

`kern_types.h` 定义了 Xeros 内核的全局基础类型体系。所有内核模块的头文件都包含此文件，这意味着这里定义的常量、枚举和类型是整个内核的"通用语言"。错误码参考 Linux errno 子集，栈管理常量考虑 520KB SRAM 的嵌入式约束。v2 新增 SMP 相关的 CPU 标识、调度类 ID 等类型。

---

## 关键概念

### 错误码体系

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L24-L38)*

```c
#define KERN_OK         0       /* 成功 */
#define KERN_ERR        (-1)    /* 通用错误 */
#define KERN_ENOENT     (-2)    /* 无此文件或目录 */
#define KERN_ENOMEM     (-12)   /* 内存不足 */
#define KERN_EACCES     (-13)   /* 权限不足 */
#define KERN_EEXIST     (-17)   /* 文件已存在 */
#define KERN_EISDIR     (-21)   /* 是目录 */
#define KERN_EINVAL     (-22)   /* 参数无效 */
#define KERN_EMFILE     (-24)   /* 打开文件过多 */
#define KERN_ENOSPC     (-28)   /* 空间不足 */
#define KERN_EPIPE      (-32)   /* 管道损坏 */
#define KERN_EBADF      (-9)    /* 无效文件描述符 */
#define KERN_ENOTTY     (-25)   /* 不支持的 ioctl */
#define KERN_ENOTEMPTY  (-39)   /* 目录非空 */
#define KERN_EPERM      (-40)   /* 操作不允许（如非 owner 解锁 mutex） */
```

#### 中文伪代码拆解

```
/* 错误码设计：
 *   0 = 成功
 *   负数 = 错误，绝对值参考 Linux errno
 *
 * 所有返回 int 类型的内核函数遵循：
 *   >= 0 表示成功（FD/PID/字节数）
 *   <  0 表示错误码
 *
 * 这允许返回值既承载成功数据（如 FD=3），
 * 又承载错误信息（如 KERN_ENOMEM=-12）
 */
```

**设计原则**：错误码为负数方便在 `>=0` 检测中一次判断成功/失败。文件描述符、PID 都是非负值，所以负数不会与有效值冲突。

### 任务相关常量

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L45-L50)*

```c
typedef int16_t kern_pid_t;

#define KERN_PID_INVALID   (-1)   /* 无效 PID */
#define KERN_MAX_TASKS     16     /* 最大并发任务数 */
#define KERN_TASK_NAME_LEN 16     /* 任务名称最大长度 */
#define KERN_CPU_ANY       0xFF   /* 自动分配 CPU（SMP） */
```

#### 新增类型说明

| 常量 | 值 | 引入版本 | 说明 |
|------|-----|----------|------|
| `KERN_CPU_ANY` | `0xFF` | v2 | SMP 自动 CPU 分配标记。当 `cpu_id == KERN_CPU_ANY` 时，`kern_spawn()` 将任务分配到当前 CPU 或负载最低的核心 |
| `KERN_PID_INVALID` | `-1` | v1 | 无效 PID，用于函数返回错误时的哨兵值 |

**限制说明**：
- `MAX_TASKS=16`：在 520KB SRAM 下，每个任务至少 1KB 栈 + ~150B TCB，16 个任务已经接近内存极限
- `TASK_NAME_LEN=16`：包括结尾 `\0`，实际可用 15 个字符，足够区分任务（如 "ui"、"shell"、"wifi_daemon"）

### 任务状态枚举

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L55-L61)*

```c
typedef enum {
    KERN_TASK_READY    = 0,  /* 就绪 */
    KERN_TASK_RUNNING  = 1,  /* 运行中 */
    KERN_TASK_SLEEPING = 2,  /* 睡眠中 */
    KERN_TASK_BLOCKED  = 3,  /* 阻塞中（等待 I/O 等） */
    KERN_TASK_ZOMBIE   = 4,  /* 僵尸（已退出，待回收） */
} kern_task_state_t;
```

```
任务状态机（v2）：

                 ┌→ SLEEPING ──(到期)──┐
                 │                    │
  SPAWN → READY → RUNNING → READY  ←─┘
               ↑         │
               │         └→ ZOMBIE (exit)
               │
             (yield / 时间片用尽 / 抢占)

  BLOCKED 保留用于未来阻塞 I/O
  ZOMBIE 任务不立即释放 TCB，由 reap_zombies() 延迟回收
```

### 日志级别

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L65-L71)*

```c
typedef enum {
    KERN_LOG_DEBUG = 0,  /* 调试信息 */
    KERN_LOG_INFO  = 1,  /* 一般信息 */
    KERN_LOG_WARN  = 2,  /* 警告 */
    KERN_LOG_ERROR = 3,  /* 错误 */
    KERN_LOG_PANIC = 4,  /* 致命错误（触发 panic） */
} kern_log_level_t;
```

设置日志级别为 `KERN_LOG_WARN` 时，只有 WARN、ERROR、PANIC 的消息才会输出。在嵌入式设备上，这可以大幅减少串口输出开销。

### 文件类型与描述符

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L75-L87)*

```c
typedef enum {
    KERN_FILE_REGULAR = 0,  /* 普通文件 */
    KERN_FILE_DIR     = 1,  /* 目录 */
    KERN_FILE_CHRDEV  = 2,  /* 字符设备 */
    KERN_FILE_FIFO    = 4,  /* 命名管道 */
} kern_file_type_t;

typedef int16_t kern_fd_t;
#define KERN_FD_INVALID    (-1)
#define KERN_MAX_FD_PER_TASK 8   /* 每个任务最大文件描述符数 */
```

每个任务最多同时打开 **8 个文件描述符**。典型的 UI 任务可能只需要打开 `/dev/fb0` 和一个 pipe 读端。

### 栈管理常量

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L91-L94)*

```c
#define KERN_STACK_MIN      1024  /* 初始栈大小（字节） */
#define KERN_STACK_MAX      8192  /* 最大栈大小（字节） */
#define KERN_STACK_GROW     1024  /* 每次增长步进（字节） */
#define KERN_STACK_CANARY   0xDEADC0DE  /* 栈金丝雀值（溢出检测） */
```

```
/* 动态栈策略：
 *   1. 任务创建时分配 MIN (1KB)
 *   2. 每次 tick 检查使用率
 *   3. 使用率 > 75% 时触发警告日志
 *   4. 金丝雀值被覆盖 → kern_panic()
 */
```

### 路径常量

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L98-L99)*

```c
#define KERN_PATH_MAX       64    /* 最大路径长度 */
#define KERN_NAME_MAX       31    /* 文件名最大长度 */
```

路径 64 字节足够支撑 `/dev/some_longish_device_name\0`，文件名 31 字节与 FAT32 兼容。

### (v2新增) 调度器相关类型

以下类型由调度类子系统的头文件定义，但它们是内核类型体系的重要组成部分：

| 类型 | 定义位置 | 说明 |
|------|----------|------|
| `kern_sched_class_t` | `kern_sched_class.h` | 调度器类操作表（含 `enqueue/dequeue/pick_next/tick/prio_changed` 函数指针） |
| `KERN_SCHED_MAX_CLASSES` | `kern_sched_class.h` | 最大可注册调度类数量（=8） |
| `SCHED_RR_DEFAULT_TIMESLICE` | `kern_sched_rr.h` | RR 默认时间片（=10 tick） |

调度类 ID（`scheduler_class_id`）是 `int8_t` 类型，存储在 TCB 中。正值表示已分配，`-1` 表示未分配。详见 [可插拔调度类](kern-sched-class.md)。

### (v2新增) SMP 与同步类型

| 类型 | 定义位置 | 说明 |
|------|----------|------|
| `kern_per_cpu_t` | `kern_smp.h` | Per-CPU 数据结构（CPU编号/当前任务/idle/调度计数/抢占标记） |
| `KERN_MAX_CPUS` | `kern_smp.h` | 最大 CPU 数量（=2，对应 ESP32 PRO_CPU + APP_CPU） |
| `spinlock_t` | `kern_sync.h` | 自旋锁（SMP 模式用原子操作，单核退化空操作） |
| `mutex_t` | `kern_sync.h` | 互斥锁（自旋锁保护的所有者检查 + 等待队列） |

详见 [SMP 支持](kern-smp.md) 和 [同步原语](kern-sync.md)。

---

## 与其他组件的关系

- **所有内核模块**：通过 `#include "kern_types.h"` 共享错误码和常量
- **kern_task**：使用 `kern_task_state_t`、`MAX_TASKS`、栈常量、`KERN_CPU_ANY`
- **kern_vfs**：使用 `kern_file_type_t`、`kern_fd_t`、路径常量
- **kern_init**：使用 `kern_log_level_t`

---

> **See Also:** [调度器](kern-task.md) | [SMP 支持](kern-smp.md) | [可插拔调度类](kern-sched-class.md) | [同步原语](kern-sync.md) | [编码风格规范](../coding-style.md)
