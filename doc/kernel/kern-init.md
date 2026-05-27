# 内核初始化与日志系统（Kern Init）

> **Parent:** [内核总览](index.md) | **Related:** [类型系统](kern-types.md), [调度器](kern-task.md)

## 概述

`kern_init` 模块实现了 Xeros 内核的启动入口、分级日志输出、致命错误 panic 处理。它是内核最早初始化的模块，提供调试和错误追踪的基础设施。

---

## 关键概念

### 内核初始化（幂等性）

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L35-L50)*

```c
void kern_init(void)
{
    if (g_kern_initialized) {
        g_init_count++;
        return;
    }
    g_kern_initialized = true;
    g_init_count = 1;

#ifndef NATIVE_TEST
    g_log_mutex = xSemaphoreCreateMutex();
#endif

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

    /* ESP32 环境：创建日志互斥锁 */
    创建互斥锁()

    日志_输出(信息级别, "Xeros内核已初始化")
}
```

**幂等性设计**：`kern_init()` 可被多次调用而不会有副作用。每个依赖内核初始化的子系统（VFS、devfs、sched）都会先调用 `kern_init()`，但实际初始化只执行一次。`g_init_count` 追踪调用次数，用于统计信息。

### 分级日志系统

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L76-L111)*

```c
void kern_log(kern_log_level_t level, const char *fmt, ...)
{
    if (level < g_log_level) return;  // 级别过滤
    va_list args;
    va_start(args, fmt);
    kern_vlog(level, fmt, args);
    va_end(args);
}

void kern_vlog(kern_log_level_t level, const char *fmt, va_list args)
{
#ifdef NATIVE_TEST
    fprintf(stdout, "[%s] ", log_level_str(level));
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
#else
    /* ESP32：互斥保护 + 串口输出 */
    xSemaphoreTake(g_log_mutex, portMAX_DELAY);
    debug_printf("[%s] ", log_level_str(level));
    debug_vprintf(fmt, args);
    debug_printf("\n");
    xSemaphoreGive(g_log_mutex);
#endif
}
```

#### 中文伪代码拆解

```
函数 内核日志(级别, 格式化字符串, ...) {
    if (级别 < 当前日志阈值) return   // 静默过滤低级别消息

    格式化参数列表
    内核日志_va版本(级别, 格式化字符串, 参数列表)
}

函数 内核日志_va版本(级别, 格式化字符串, 参数列表) {
    if (级别 < 当前日志阈值) return

    if (是本地测试环境) {
        用 fprintf 输出到 stdout    // PC 上直接用标准输出
    } else {
        /* ESP32 */
        获取互斥锁()                // 防止多任务竞争串口
        debug输出前缀("[级别] ")
        debug格式化输出(格式化字符串, 参数列表)
        debug输出换行()
        释放互斥锁()
    }
}
```

**双平台考虑**：
- **Native**：使用标准 C `fprintf(stdout, ...)`，在 GoogleTest 中可直接捕获输出用于断言
- **ESP32**：通过互斥锁保护串口写入，避免多个 FreeRTOS 任务同时往串口输出导致乱码

### Panic 处理

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L115-L133)*

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
    while (1) { /* 硬件环境：无限循环，LED 闪烁 */ }
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
        无限循环 {
            /* 硬件LED闪烁，提示用户发生了致命错误 */
        }
    }
    /* Native 环境：允许继续执行（用于测试验证 panic 行为） */
}
```

**设计要点**：
- `g_last_panic` 静态缓冲区保存最后一次 panic 的消息，通过 `kern_get_last_panic()` 查询
- `kern_clear_panic()` 用于测试环境重置 panic 状态
- Native 环境下 `kern_panic()` 不会真正停止程序，方便在测试中断言 `kern_get_last_panic()` 的内容

### 统计信息

*📄 Source: [kern_init.c](../../src/kernel/kern_init.c#L148-L156)*

`kern_log_stats()` 打印内核运行时统计信息：是否已初始化、初始化调用次数、当前日志级别、是否发生过 panic。这是一个调试辅助函数，供 Shell 的 `stats` 类命令或自动监控使用。

---

## 与其他组件的关系

- **kern_task**：在 `kern_sched_init()` 内部调用 `kern_log()` 输出调度器初始化信息；内存分配失败时调用 `kern_panic()`
- **kern_vfs**：VFS 初始化时输出 `kern_log()` 信息
- **kern_shell**：可以通过 Shell 命令查询内核统计信息和设置日志级别

---

> **See Also:** [类型系统](kern-types.md) | [调度器](kern-task.md) | [Shell](kern-shell.md)
