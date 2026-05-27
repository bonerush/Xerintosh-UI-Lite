# 协作式调度器（Kern Task）

> **Parent:** [内核总览](index.md) | **Related:** [类型系统](kern-types.md), [VFS](kern-vfs.md), [系统调用](kern-syscall.md), [IPC](kern-ipc.md)

## 概述

`kern_task` 实现了 Xeros 内核的核心组件 —— 纯用户态、手写 Round-Robin 协作式调度器。与 FreeRTOS 抢占式不同，任务需**主动调用 `sys_yield()` 让出 CPU**。这一设计消除了锁机制需求，大幅简化了嵌入式代码的复杂度。

支持的调度原语：
- `kern_spawn(name, entry, stack_extra)` — 创建并启动新任务
- `sys_yield()` — 主动让出 CPU，调度器选择下一个就绪任务
- `sys_sleep_ms(ms)` — 进入睡眠，ms 毫秒后自动唤醒
- `sys_exit(ret)` — 退出当前任务

---

## 关键概念

### 任务控制块（TCB）

*📄 Source: [kern_task.h](../../src/kernel/kern_task.h#L20-L47)*

```c
typedef struct kern_task {
    char            name[KERN_TASK_NAME_LEN];  /* 任务名 */
    kern_pid_t      pid;                       /* 进程 ID */
    kern_task_state_t state;                   /* 当前状态 */

    /* 协作式上下文保存 */
    void          *stack_base;                 /* 用户态栈底部指针 */
    void          *stack_ptr;                  /* 当前栈顶指针 */
    size_t         stack_size;                 /* 当前分配栈大小 */
    unsigned char *canary;                     /* 金丝雀位置 */
    unsigned int   last_sp_used;               /* 上次检查的 SP 使用量 */

    /* 入口点与返回 */
    kern_task_entry_func_t entry;              /* 任务入口函数 */
    void          *arg;                        /* 用户参数 */
    int            retval;                     /* 退出返回值 */

    /* 文件描述符表 */
    kern_file_t   *fd_table[KERN_MAX_FD_PER_TASK];

    /* 睡眠/阻塞计时 */
    uint32_t       wake_at_ms;                 /* 唤醒时间戳（millis） */
    struct kern_task *blocked_on;              /* 阻塞等待的目标任务 */

    /* 链表指针 */
    struct kern_task *next;
    struct kern_task *prev;
}
```

#### 中文伪代码拆解

```
结构体 任务控制块 {
    /* 基础信息 */
    任务名称[16字节]
    进程ID
    当前状态 {就绪/运行/睡眠/阻塞/僵尸}

    /* 动态栈管理 */
    栈底指针        指针类型
    栈顶指针        指针类型
    当前栈大小      无符号整数
    金丝雀位置      指针类型 → 指向栈底的金丝雀标记
    上次SP使用量    用于检测接近上限

    /* 入口信息 */
    任务入口函数指针
    用户参数指针
    返回值

    /* 资源 */
    文件描述符表[8]  ← 每个任务最多打开8个文件

    /* 调度状态 */
    唤醒时间戳      毫秒级，用于睡眠到期检查
    阻塞等待对象    正在等待的另一个任务（IPC场景）

    /* 双向循环链表 */
    next指针、prev指针   ← 所有任务串联成环形链表
}
```

### 调度循环（Round-Robin）

*📄 Source: [kern_task.c](../../src/kernel/kern_task.c#L233-L283)*

```c
void kern_schedule(void)
{
    current_task = current_task->next;

    /* 跳过非就绪态任务 */
    while (current_task != NULL &&
           current_task->state == KERN_TASK_SLEEPING)
    {
        /* 检查睡眠是否到期 */
        current_task = current_task->next;
    }
}
```

#### 中文伪代码拆解

```
全局状态：
    当前任务指针 → 指向正在运行的任务TCB
    睡眠唤醒列表 → 按到期时间排序的任务集合

函数 内核调度() {
    执行上下文保存：
        if (当前任务 != NULL) 调用栈检查函数(当前任务)
        if (当前任务 != NULL) 任务状态设为就绪
        if (当前任务是僵尸态) 调用任务终止清理函数(当前任务)

    任务选择：
        移动当前任务指针到下一个
        循环跳过睡眠中且未到期的任务：
            移动到下一个

        if (遍历一圈后没有就绪任务) {
            if (有idle任务) 用空闲任务兜底
            else return  // 没有可运行的任务，退出
        }

    执行上下文恢复：
        当前任务状态设为运行
        恢复当前任务的栈上下文

    /* 控制权转移到新任务，从这里开始执行 */
}
```

调度策略：纯 Round-Robin（FIFO），无优先级抢占。睡眠任务是唯一会导致被跳过的状态。因所有任务运行在同一个线程，上下文切换就是简单的栈指针交换。

### 上下文保存与恢复（`setjmp` 协议）

*📄 Source: [kern_task.c](../../src/kernel/kern_task.c#L56-L91)*

```
ESP32（FreeRTOS 令牌协议）：
    yield_count 计数器 → 任务切换时++，任务循环检查变化即重入调度

Native（POSIX ucontext）：
    使用 getcontext/makecontext/swapcontext
    每个任务独立的栈 + 完整的通用寄存器保存
```

**为什么两个平台用不同策略？**

| 策略 | ESP32 | Native |
|------|-------|--------|
| 机制 | FreeRTOS 令牌计数 | `ucontext` (POSIX) |
| 原因 | FreeRTOS 上下文切换危险且不可移植 | 桌面环境 `ucontext` 更完整且易调试 |
| 切换开销 | 极低（仅一个整数比较） | 中等（寄存器 + 信号掩码） |
| 栈隔离 | 同一 FreeRTOS 任务栈内切换 SP | 独立 `ucontext_t` 分配的栈 |

ESP32 方案的关键洞见：所有 Xeros "任务" 都运行在同一个 FreeRTOS 任务（Arduino `loop()`）内，所以只需要切换**用户态概念上的栈顶指针**，不需要真实地切换 FreeRTOS 任务。

### 动态栈管理

*📄 Source: [kern_task.c](../../src/kernel/kern_task.c#L95-L157)*

```c
void kern_check_stack(const kern_task_t *task)
{
    /* 防范措施1：检查金丝雀 */
    if (task->canary != NULL) {
        bool ok = (*task->canary == (KERN_STACK_CANARY & 0xFF));
        if (!ok) {
            kern_panic("栈金丝雀被覆盖：溢出！");
            return;
        }
    }

    /* 防范措施2：估算使用量 */
    unsigned int used = (unsigned int)(
        (unsigned char *)task->stack_base + task->stack_size - task->stack_ptr);

    /* 防范措施3：使用率 > 80% 且未达上限 → 扩展栈 */
    if (used > task->stack_size * 80 / 100 && task->stack_size < KERN_STACK_MAX) {
        size_t new_size = task->stack_size + KERN_STACK_GROW;
        kern_grow_stack(task, new_size);
    }
}
```

#### 中文伪代码拆解

```
函数 栈检查(任务TCB指针) {
    /* 金丝雀检测 */
    if (金丝雀位置的值 != 0xAD) {  // canary = 0xDEADC0DE & 0xFF
        内核致命错误("栈溢出！金丝雀被踩")
    }

    /* 用量估算 */
    已用量 = (栈底 + 栈大小 - 当前SP)

    /* 自动扩展 */
    if (已用量 > 栈大小 * 80% 且 栈大小 < 最大上限8KB) {
        新大小 = 当前大小 + 1KB
        重新分配内存(新大小)
        // 注意：新版栈底在低地址方向偏移了1KB
        // 金丝雀位置自动移动到新的栈底
    }
}
```

**动态栈机制的三层防线：**

| 层级 | 检测机制 | 触发条件 | 行为 |
|------|----------|----------|------|
| 1 | 金丝雀（栈底 1 字节） | 值 != 0xAD | `kern_panic()` 立即终止 |
| 2 | SP 使用率检查 | 使用率 > 80% | 自动 `realloc` 扩展 +1KB |
| 3 | 上限拒绝 | 栈大小 >= 8KB | 拒绝扩展，任务继续运行 |

### 任务生命周期

*📄 Source: [kern_task.c](../../src/kernel/kern_task.c#L185-L230)*

```c
void kern_task_create(kern_task_t *task, const char *name,
                       kern_task_entry_func_t entry, void *arg,
                       size_t stack_extra)
{
    size_t stack_alloc = KERN_STACK_MIN + stack_extra + 256;
    task->stack_size = stack_alloc;
    task->stack_base = kern_malloc(stack_alloc);
    task->stack_ptr = (char *)task->stack_base + stack_alloc - 16;
    task->canary = (unsigned char *)task->stack_base;
    *(task->canary) = (unsigned char)(KERN_STACK_CANARY & 0xFF);
    // ... 设置栈上的入口点 ...
}
```

#### 中文伪代码拆解

```
任务生命周期：

1. SPAWN阶段
   └→ kern_spawn("ui", ui_task_main, 0)
       ├→ 分配 TCB（calloc）
       ├→ 分配栈（KERN_STACK_MIN + 额外 + 256字节余量）
       ├→ 设置金丝雀 = 0xAD
       ├→ 在栈顶写入入口函数地址 + 参数
       ├→ PID 递增分配
       ├→ 插入任务链表尾部
       └→ 状态设为 READY

2. RUNNING阶段
   └→ 调度器选中该任务后
       ├→ 状态设为 RUNNING
       ├→ 恢复上下文（栈顶指针切换到该任务的栈）
       └→ 执行入口函数

3. YIELD阶段
   └→ 任务调用 sys_yield()
       ├→ 状态设为 READY
       ├→ 栈检查（金丝雀 + 使用率）
       └→ 调度器选择下一个 READY 任务

4. SLEEP阶段
   └→ 任务调用 sys_sleep_ms(500)
       ├→ 状态设为 SLEEPING
       ├→ 设置 wake_at_ms = current_millis + 500
       └→ 调度器在循环中跳过 SLEEPING 且未到期的任务
       └→ 到期后状态恢复为 READY

5. EXIT阶段
   └→ 任务调用 sys_exit(0)
       ├→ 状态设为 ZOMBIE
       ├→ retval 记录返回值
       └→ 调度器在下次遇到僵尸任务时调用 kern_task_cleanup()
           └→ 释放栈内存、关闭所有文件描述符、释放 TCB
```

### 空闲任务（Idle Task）

调度器初始化时自动创建 `[idle]` 任务。当没有就绪任务时启动 idle 任务，避免调度器无所适从。idle 任务内部由 `kern_idle_entry()` 实现，循环调用 `sys_yield()`。

### 虚任务（Virtual Task）

虚任务是为 `user_item` App 提供内核可观测性的轻量机制。

特点：
- **无独立 FreeRTOS 上下文**：不创建 FreeRTOS 任务，不参与调度
- **仅用于可观测性**：`/proc/tasks` 可见、`kill` 可终止、任务管理器可管理
- **生命周期绑定 App**：进入 user_item 时 `kern_task_register_virtual()`，退出时 `kern_task_unregister_virtual()`
- **终止行为**：被 kill 后通过 `g_xerintosh_exit_requested` 标志通知 UI 任务退出当前 App

```c
// 注册虚任务（在 user_item enter 时调用）
kern_pid_t pid = kern_task_register_virtual("serial_monitor");

// 注销虚任务（在 user_item exit 时调用）
kern_task_unregister_virtual(pid);
```

虚任务在 `/proc/tasks` 中的 stack 字段显示为 `n/a`，因为其没有独立栈。标志位 `KERN_TASK_FLAG_VIRTUAL` 用于区分虚任务和真实任务。

---

## 与其他组件的关系

- **kern_syscall**：`sys_yield`、`sys_sleep_ms`、`sys_exit` 通过系统调用分发器路由到调度器
- **kern_vfs**：每个 task 的 `fd_table` 引用 VFS 文件对象，任务退出时自动关闭所有 fd
- **kern_ipc**：管道的阻塞读使用 `blocked_on` 字段等待写入任务
- **kern_procfs**：`/proc/tasks` 遍历调度器链表显示任务信息
- **kern_shell**：Shell 的 `ps` 命令通过 `kern_task_get_by_pid()` 遍历任务链表

---

> **See Also:** [类型系统](kern-types.md) | [IPC](kern-ipc.md) | [系统调用](kern-syscall.md) | [调度器计划](../../.claude/plans/unified-cooperative-scheduler.plan.md)
