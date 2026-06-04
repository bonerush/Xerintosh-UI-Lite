# 内核优化分析报告

> **Parent:** [知识地图](../index.md) | **Related:** [内核总览](kernel/index.md), [调度器](kernel/kern-task.md), [VFS](kernel/kern-vfs.md), [Shell](kernel/kern-shell.md)

> **来源**: 4 模块协同优化计划 Phase 4
> **目标板**: M5Stick-C (ESP32-PICO)
> **分析日期**: 2026-05-28

---

## 概述

本文对 Xeros 微内核的 6 个核心模块进行诊断、方案设计、代码示例和收益评估。每个模块独立可实施，按投入产出比排序。

---

## 1. 调度器优化

### 问题诊断

- **纯 Round-Robin 无优先级**：单个失控任务可饿死 UI 线程
- **无 CPU 占用统计**：`/proc/tasks` 不暴露执行时间，无法诊断 CPU 热点
- **Idle 任务无省电**：空闲时 CPU 始终全速运行，浪费功耗

### 优化方案

#### 1.1 TCB 扩展 — 执行统计

```c
// kern_task.h — TCB 扩展字段
typedef struct kern_task {
    // ... 现有字段 ...
    uint32_t exec_ticks;     /* 累计运行 tick 数 */
    uint32_t last_run;       /* 上次调度时间戳 */
} kern_task_t;
```

在 `kern_sched_tick()` 中每次调度时递增 `task->exec_ticks`。

#### 1.2 两级优先级队列

就绪队列拆分为 `high_prio`（UI/Shell，优先级 128+）和 `low_prio`（后台，<128），按 2:1 比例调度：

```c
static kern_task_t *pick_next_ready(void) {
    static int balance = 0;
    if (balance < 2 && has_high_prio_ready())
        return pick_high_prio();
    balance = (balance + 1) % 3;
    return round_robin_next();
}
```

#### 1.3 Idle 省电

```c
// kern_sched.c — idle 分支
if (no_ready_tasks()) {
    esp_light_sleep_start();  // 自动唤醒由 FreeRTOS tickless 处理
}
```

### 预期收益

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| UI 最坏延迟 | O(N×tick) | O(1) |
| Idle 功耗 | ~60mA | ~40mA（light sleep） |
| CPU 可见性 | 无 | `/proc/tasks` CPU% |

---

## 2. 动态栈管理优化

### 问题诊断

- **`realloc` 碎片化**：栈空间动态扩容产生外部碎片和不确定延迟
- **溢出检测滞后**：仅在栈真正越界时触发 panic，无预警机制

### 优化方案

#### 2.1 两级固定栈池

替代动态分配，消除碎片：

```c
#define POOL_SMALL_SIZE  1024
#define POOL_LARGE_SIZE  4096
#define POOL_SMALL_COUNT 4
#define POOL_LARGE_COUNT 2

typedef struct {
    uint8_t  buf[POOL_SMALL_SIZE];
    bool     in_use;
    kern_pid_t owner;
} small_slot_t;

typedef struct {
    uint8_t  buf[POOL_LARGE_SIZE];
    bool     in_use;
    kern_pid_t owner;
} large_slot_t;

static small_slot_t g_small_pool[POOL_SMALL_COUNT];
static large_slot_t g_large_pool[POOL_LARGE_COUNT];
```

#### 2.2 栈溢出增强报告

```c
void kern_stack_overflow_panic(kern_task_t *task, uint32_t canary) {
    KERN_LOG_PANIC("STACK OVERFLOW: task='%s'(pid=%d), "
                   "used=%zu/%zu, canary=0x%08X (expected 0x%08X)",
                   task->name, task->pid,
                   kern_task_stack_usage(task), task->stack_size,
                   canary, KERN_STACK_CANARY);
}
```

### 预期收益

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 分配/释放 | O(N) realloc | O(1) 池索引 |
| 碎片风险 | 存在 | 消除 |
| 溢出预警 | 仅 crash | 80% 阈值 + canary 扫描 |

---

## 3. VFS 与文件系统优化

### 问题诊断

- **路径解析无缓存**：每次 `kern_path_resolve()` 完整遍历目录树
- **动态内容重复生成**：`/proc/meminfo` 每次读取重新计算

### 优化方案

#### 3.1 路径解析单条目缓存

```c
static kern_dentry_t *g_last_dentry = NULL;
static char g_last_path[KERN_PATH_MAX];

kern_dentry_t *kern_path_resolve(const char *path) {
    if (g_last_dentry && strcmp(path, g_last_path) == 0)
        return g_last_dentry;  /* 缓存命中 */
    // ... 完整路径解析 ...
    strncpy(g_last_path, path, KERN_PATH_MAX);
    g_last_dentry = result;
    return result;
}
```

Shell 场景中路径重复率高（`ls`、`cat` 连续操作同一目录），命中率 >90%。

#### 3.2 动态内容单次生成

```c
static ssize_t procfs_meminfo_generate(char *buf, size_t len) {
    return snprintf(buf, len,
        "MemTotal: %" PRIu32 "\n"
        "MemFree:  %" PRIu32 "\n"
        "MemUsed:  %" PRIu32 "\n"
        "MinFree:  %" PRIu32 "\n",
        total, free, used, min_free);
}
```

### 预期收益

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 路径解析延迟 | O(depth × children) 每次 | 缓存命中 O(1) |
| `/proc` 读取 | 多次函数调用 | 单次 snprintf |

---

## 4. Shell 与系统调用

### 问题诊断

- **参数校验分散**：每个 sysfs write handler 独立校验，错误码不一致
- **无脚本能力**：自动化标定需 PC 连接，现场无法执行

### 优化方案

#### 5.1 统一参数校验

```c
static ssize_t sysfs_write(kern_file_t *f, const char *buf, size_t len) {
    kern_sysfs_attr_t *attr = (kern_sysfs_attr_t *)f->inode->private_data;
    long val = parse_long(buf, len);
    if (val < attr->min || val > attr->max)
        return KERN_EINVAL;  /* 统一错误码 */
    attr->current_value = (int32_t)val;
    /* 触发硬件绑定回调 */
    for (int i = 0; i < attr->callback_count; i++)
        attr->callbacks[i](attr, attr->callbacks_user_data[i]);
    return len;
}
```

#### 5.2 脚本引擎最小实现

```c
typedef struct {
    char   name[32];
    char  *lines[64];
    int    line_count;
    int    pc;          /* 程序计数器 */
    bool   running;
} script_t;

// 支持原语：echo, sleep, if-goto, assert, wait
// script run /calibration.scr
```

### 预期收益

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 参数校验 | 分散，错误码不一致 | 统一 EINVAL |
| 自动化 | 需 PC 连接 | 脚本引擎本地执行 |

---

## 5. 内核可观测性

### 问题诊断

- **栈溢出不可预见**：仅 crash 时才知溢出，缺乏预警
- **CPU 占用不可见**：无法定位 CPU 热点
- **`/proc/tasks` 信息不够**：缺少 CPU%、栈使用率百分比

### 优化方案

#### 6.1 TCB exec_ticks 暴露

```c
// procfs_tasks_generate() 增加
pos += snprintf(buf + pos, len - pos,
    "%-4d %-8s %-12s %zu/%-4zu %3u%%\n",
    task->pid, state_str, task->name,
    usage, task->stack_size,
    (unsigned)(task->exec_ticks * 100 / total_ticks));
```

#### 6.2 栈金丝雀周期扫描

```c
void kern_stack_canary_scan_all(void) {
    kern_task_t *t = g_task_list_head;
    while (t) {
        if (t->state == KERN_TASK_ZOMBIE) { t = t->next; continue; }
        uint32_t *canary = (uint32_t *)(t->stack_base + t->stack_size - 4);
        if (*canary != KERN_STACK_CANARY) {
            KERN_LOG_PANIC("canary corrupted in '%s'(pid=%d)", t->name, t->pid);
        }
        size_t usage = kern_task_stack_usage(t);
        if (usage > t->stack_size * 80 / 100) {
            KERN_LOG_WARN("stack >80%% in '%s'(pid=%d): %zu/%zu",
                          t->name, t->pid, usage, t->stack_size);
        }
        t = t->next;
    }
}
// 在 kern_sched_tick() 的 idle 分支每 10 tick 调用一次
```

### 预期收益

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 栈溢出预警 | 仅 crash | 80% 阈值 + canary 周期扫描 |
| CPU 占用 | 不可见 | `/proc/tasks` CPU% 列 |
| 诊断效率 | 需 gdb/JTAG | Shell 命令即可定位 |

---

## 6. 编译与内存优化

### 问题诊断

- **字符串常量占用 DRAM**：帮助文本、命令名等只读字符串在 SRAM 中
- **热路径函数调用开销**：调度器 `pick_next_ready()` 等频繁调用

### 优化方案

#### 7.1 PROGMEM 常量存储

```c
// kern_shell_cmds.c
static const char HELP_LS[] PROGMEM = "list directory";
static const char HELP_CD[] PROGMEM = "change directory";

static const shell_cmd_t g_builtin_cmds[] = {
    { "ls", cmd_ls, HELP_LS },
    { "cd", cmd_cd, HELP_CD },
    // ...
};
// 读取时: strcpy_P(buf, cmd->help);
```

预计节省 ~1.2KB SRAM（20+ 条命令的帮助文本移入 Flash）。

#### 7.2 热路径内联

```c
// kern_task.c
static inline __attribute__((always_inline))
kern_task_t *pick_next_ready(void) { ... }

// 调度器文件级优化
#pragma GCC optimize("O3")
```

### 预期收益

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| SRAM 占用 | 含 ~1.2KB 字符串 | 省 ~1.2KB |
| 调度热路径 | 2-3 函数调用 | 内联消除 |

---

## 总结

| 模块 | 难度 | 收益 | 风险 | 推荐优先级 |
|------|------|------|------|-----------|
| 调度器 | 中 | 高（UI 延迟 + 省电） | 低 | 1 |
| 栈管理 | 低 | 中（碎片消除 + 预警） | 低 | 2 |
| VFS | 低 | 中（路径缓存命中 >90%） | 低 | 3 |
| Shell | 中 | 高（自动化标定） | 中（脚本引擎） | 4 |
| 可观测性 | 低 | 高（诊断效率） | 低 | 5 |
| 编译内存 | 低 | 中（省 ~1.2KB SRAM） | 低 | 6 |

全部 6 模块可按任意顺序独立实施，互不阻塞。
