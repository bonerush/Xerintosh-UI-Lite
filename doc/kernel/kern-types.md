# 内核类型系统（Kern Types）

> **Parent:** [内核总览](index.md) | **Related:** [初始化与日志](kern-init.md), [调度器](kern-task.md), [VFS](kern-vfs.md)

## 概述

`kern_types.h` 定义了 Xeros 内核的全局基础类型体系。所有内核模块的头文件都包含此文件，这意味着这里定义的常量、枚举和类型是整个内核的"通用语言"。错误码参考 Linux errno 子集，栈管理常量考虑 520KB SRAM 的嵌入式约束。

---

## 关键概念

### 错误码体系

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L24-L39)*

```c
#define KERN_OK         0       /* 成功 */
#define KERN_ERR        (-1)    /* 通用错误 */
#define KERN_ENOENT     (-2)    /* 无此文件或目录 */
#define KERN_EIO        (-5)    /* I/O 错误 */
#define KERN_ENOMEM     (-12)   /* 内存不足 */
#define KERN_EACCES     (-13)   /* 权限不足 */
#define KERN_EEXIST     (-17)   /* 文件已存在 */
#define KERN_ENOTDIR    (-20)   /* 不是目录 */
#define KERN_EISDIR     (-21)   /* 是目录 */
#define KERN_EINVAL     (-22)   /* 参数无效 */
#define KERN_EMFILE     (-24)   /* 打开文件过多 */
#define KERN_ENOSPC     (-28)   /* 空间不足 */
#define KERN_EPIPE      (-32)   /* 管道损坏 */
#define KERN_EBADF      (-9)    /* 无效文件描述符 */
#define KERN_ENOTTY     (-25)   /* 不支持的 ioctl */
#define KERN_ENOTEMPTY  (-39)   /* 目录非空 */
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

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L43-L47)*

```c
typedef int16_t kern_pid_t;
#define KERN_PID_INVALID   (-1)
#define KERN_MAX_TASKS     16
#define KERN_TASK_NAME_LEN 16
```

**限制说明**：
- `MAX_TASKS=16`：在 520KB SRAM 下，每个任务至少 1KB 栈 + ~100B TCB，16 个任务已经接近内存极限
- `TASK_NAME_LEN=16`：包括结尾 `\0`，实际可用 15 个字符，足够区分任务（如 "ui"、"shell"、"wifi_daemon"）

### 任务状态枚举

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L52-L58)*

```c
typedef enum {
    KERN_TASK_READY    = 0,  /* 就绪：可被调度 */
    KERN_TASK_RUNNING  = 1,  /* 运行中：当前持有 CPU */
    KERN_TASK_SLEEPING = 2,  /* 睡眠中：等待到期唤醒 */
    KERN_TASK_BLOCKED  = 3,  /* 阻塞中：等待 I/O */
    KERN_TASK_ZOMBIE   = 4,  /* 僵尸：已退出，待回收 */
} kern_task_state_t;
```

#### 中文伪代码拆解

```
/* 任务状态机：
 *
 *                    ┌→ SLEEPING ──(到期)──┐
 *                    │                    │
 *   SPAWN → READY → RUNNING → READY  ←───┘
 *                ↑         │
 *                │         └→ ZOMBIE (exit)
 *                │
 *              (yield 后回到就绪)
 *
 * BLOCKED 状态保留用于未来阻塞 I/O（如按键 read 等待数据）
 * ZOMBIE 任务不立即释放 TCB，由内核在适当时机回收
 */
```

### 日志级别

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L62-L68)*

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

### 栈管理常量

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L89-L92)*

```c
#define KERN_STACK_MIN      1024  /* 初始栈大小（字节） */
#define KERN_STACK_MAX      8192  /* 最大栈大小（字节） */
#define KERN_STACK_GROW     1024  /* 每次增长步进（字节） */
#define KERN_STACK_CANARY   0xDEADC0DE  /* 栈金丝雀值 */
```

#### 中文伪代码拆解

```
/* 动态栈策略：
 *   1. 任务创建时分配 MIN (1KB)
 *   2. 每次 yield 检查使用率
 *   3. 使用率 > 80% 时 realloc 扩展 GROW (1KB)
 *   4. 达到 MAX (8KB) 后拒绝扩展
 *
 * 金丝雀检测：
 *   栈底 4 字节写入 0xDEADC0DE
 *   如果该值被覆盖 → 栈溢出 → kern_panic()
 */
```

### 路径常量

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L96-L97)*

```c
#define KERN_PATH_MAX  64    /* 最大路径长度 */
#define KERN_NAME_MAX  31    /* 文件名最大长度 */
```

路径 64 字节足够支撑 `/dev/some_longish_device_name\0`，文件名 31 字节与 FAT32 兼容。

### 文件描述符相关

*📄 Source: [kern_types.h](../../src/kernel/kern_types.h#L82-L85)*

```c
typedef int16_t kern_fd_t;
#define KERN_FD_INVALID      (-1)
#define KERN_MAX_FD_PER_TASK   8
```

每个任务最多同时打开 **8 个文件描述符**。这限制了为嵌入式场景设计的简单性 —— 典型的 UI 任务可能只需要打开 `/dev/fb0` 和一个 pipe 读端。

---

## 与其他组件的关系

- **所有内核模块**：通过 `#include "kern_types.h"` 共享错误码和常量
- **kern_task**：使用 `kern_task_state_t`、`MAX_TASKS`、栈常量
- **kern_vfs**：使用 `kern_file_type_t`、`kern_fd_t`、路径常量
- **kern_init**：使用 `kern_log_level_t`

---

> **See Also:** [初始化与日志](kern-init.md) | [调度器](kern-task.md) | [VFS](kern-vfs.md) | [编码风格规范](../coding-style.md)
