# 内核初始化与日志系统（Kern Init）

> **Parent:** [内核总览](index.md) | **Related:** [类型系统](kern-types.md), [调度器](kern-task.md), [SMP 多核](kern-smp.md), [MPU 内存保护](kern-mpu.md)

## 概述

`kern_init` 模块实现了 Xeros 内核的启动入口、分级日志输出、致命错误 panic 处理。它是内核最早初始化的模块，提供调试和错误追踪的基础设施。

自 kernel-v2-phase1 起，日志互斥保护从 FreeRTOS 信号量切换为**原子自旋锁**（`__sync_lock_test_and_set`），消除了内核日志系统对 FreeRTOS 的最后依赖。

---

## 关键概念

### 内核初始化（幂等性）

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L33-L44)*

```c
void kern_init(void)
{
    if (g_kern_initialized) {
        g_init_count++;
        return;
    }

    g_kern_initialized = true;
    g_init_count = 1;

    kern_log(KERN_LOG_INFO, "Xeros kernel initialized");
}
```

#### 中文伪代码拆解

```
函数 内核初始化() {
    if (已初始化) {
        初始化次数加1
        return     // 幂等：多次调用不重复初始化
    }

    标记已初始化 = true
    初始化次数 = 1

    内核日志(信息级别, "Xeros内核已初始化")
}
```

**幂等性设计**：`kern_init()` 可被多次调用而不会有副作用。每个依赖内核初始化的子系统（VFS、devfs、sched、MPU）都会先调用 `kern_init()`，但实际初始化只执行一次。`g_init_count` 追踪调用次数，用于统计信息。

### 分级日志系统（自旋锁保护）

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L70-L101)*

```c
void kern_vlog(kern_log_level_t level, const char *fmt, va_list args)
{
    if (level < g_log_level) return;

#ifdef NATIVE_TEST
    fprintf(stdout, "[%s] ", log_level_str(level));
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
#else
    /* 硬件环境：自旋锁保护输出（无 FreeRTOS 依赖） */
    while (__sync_lock_test_and_set(&g_log_locked, true)) {
        /* 自旋等待 */
    }
    debug_printf("[%s] ", log_level_str(level));
    debug_vprintf(fmt, args);
    debug_printf("\n");
    __sync_lock_release(&g_log_locked);
#endif
}
```

#### 中文伪代码拆解

```
函数 内核日志_va版本(级别, 格式化字符串, 参数列表) {
    if (级别 < 当前日志阈值) return  // 静默过滤低级别消息

    if (是本地测试环境) {
        用 fprintf 输出到 stdout    // PC 上直接用标准输出
    } else {
        /* ESP32：原子自旋锁保护串口 */
        获取自旋锁()                // __sync_lock_test_and_set — 无 FreeRTOS 依赖
        debug输出前缀("[级别] ")
        debug格式化输出(格式化字符串, 参数列表)
        debug输出换行()
        释放自旋锁()
    }
}
```

**为什么切换为自旋锁？** 旧版使用 FreeRTOS `xSemaphoreTake/give`，但这意味着内核日志系统本身依赖 FreeRTOS——违反了「零 FreeRTOS 渗透」原则。自旋锁持有者不应长时间阻塞，短暂自旋即可，性能相当且消除了依赖。

### Panic 处理

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L106-L130)*

```c
void kern_panic(const char *msg)
{
    if (msg != NULL) {
        strncpy(g_last_panic, msg, sizeof(g_last_panic) - 1);
        g_last_panic[sizeof(g_last_panic) - 1] = '\0';
    } else {
        g_last_panic[0] = '\0';
    }
    g_has_panic = true;

    kern_log(KERN_LOG_PANIC, "KERNEL PANIC: %s", msg ? msg : "(no message)");

#ifndef NATIVE_TEST
    while (1) {
        /* TODO: 硬件 LED 闪烁 */
    }
#endif
}
```

#### 中文伪代码拆解

```
函数 内核致命错误(错误消息) {
    保存错误消息到 上次Panic缓冲区（用于事后查询）
    标记为已发生Panic

    内核日志(FATAL级别, "内核致命错误: 错误消息")

    if (是ESP32硬件环境) {
        无限循环 {}   // 硬件 LED 闪烁，提示致命错误
    }
    /* Native 环境：允许继续执行（用于测试验证 panic 行为） */
}
```

**设计要点**：
- `g_last_panic` 静态缓冲区保存最后一次 panic 的消息，通过 `kern_get_last_panic()` 查询
- `kern_clear_panic()` 用于测试环境重置 panic 状态
- Native 环境下 `kern_panic()` 不会真正停止程序，方便在测试中断言 panic 内容

---

## 内核完整初始化顺序

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c) + [kern_sched.c](../../src/kernel/kern_sched.c)*

```
main.cpp setup():
    M5.begin()                    ← 硬件初始化
    hal_display_init()            ← 显示驱动
    hal_input_init()              ← 按键驱动
    storage_init()                ← NVS 存储
    settings_load_from_storage()  ← 恢复设置

    ─── 内核层初始化 ───
    kern_init()                   ← 内核日志系统（幂等）
    kern_port_init()              ← 可移植层（FreeRTOS 信号量等）
    kern_mpu_init()               ← MPU 子系统（内核保护区 C0-C1）★ kernel-v2 新增
    kern_vfs_init()               ← VFS 根节点
    kern_devfs_init()             ← /dev 目录
    kern_sched_init()             ← 调度器 + idle 任务
        ├── kern_smp_init()       ★ kernel-v2 新增: 双核 per-CPU 数据初始化
        └── kern_spawn("idle")    ← 创建 idle 任务（含栈守卫 MPU 配置）
    kern_devices_init()           ← 注册 fb0/input0/ttyS0
    kern_sysfs_init()             ← /sys 虚拟文件
    kern_procfs_init()            ← /proc 虚拟文件

    kern_shell_init()             ← 启动 Shell 任务
    /* 后续: kern_spawn(ui_task) 等 */
```

**★ kernel-v2 新增步骤说明**：

| 步骤 | 必须前置 | 作用 |
|------|---------|------|
| `kern_mpu_init()` | `kern_init()` | 配置内核保护区（区域 C0-C1），为所有任务提供基础保护 |
| `kern_smp_init()` | `kern_init()` | 初始化 `g_per_cpu[]` 数组，使 per-CPU 宏可用。在单核 ESP32 上为 no-op |

`kern_mpu_init()` 必须在 `kern_sched_init()` 之前调用——因为调度器创建 idle 任务时会调用 `kern_mpu_setup_stack_guard()`，此时 MPU 子系统必须已初始化。

---

## 与其他组件的关系

- **kern_sched**：`kern_sched_init()` 内部调用 `kern_smp_init()` 和 `kern_mpu_setup_stack_guard()`；内存分配失败时调用 `kern_panic()`
- **kern_mpu**：`kern_mpu_init()` 在初始化序列中由 `main.cpp` 显式调用，使用 `kern_log()` 输出区域配置信息
- **kern_vfs**：VFS 初始化时输出 `kern_log()` 信息
- **kern_shell**：可以通过 Shell 命令查询内核统计信息和设置日志级别

---

> **See Also:** [类型系统](kern-types.md) | [调度器](kern-task.md) | [SMP 多核](kern-smp.md) | [MPU 内存保护](kern-mpu.md) | [可移植层](kern-port.md) | [Shell](kern-shell.md)
