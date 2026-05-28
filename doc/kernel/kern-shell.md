# 内核 Shell（Kern Shell）

> **Parent:** [内核总览](index.md) | **Related:** [VFS](kern-vfs.md), [物理设备](kern-devices.md), [调度器](kern-task.md), [/proc 与 /sys](kern-procfs-sysfs.md)

## 概述

`kern_shell` 实现了一个通过 **USB 串口** 交互的微型命令行解释器。它作为独立的 Xeros 任务运行，读取 `/dev/ttyS0` 的输入字符，解析命令，执行相应操作，结果输出到串口。

这是一个"活的内核调试器"——允许在运行时查看任务列表、浏览文件系统、读写 `/sys` 节点、实时监测数据、甚至执行 OTA 升级和恢复出厂设置。

---

## 关键概念

### Shell 任务启动

*📄 Source: [kern_shell.c](../../src/kernel/kern_shell.c#L734-L746)*

```c
void kern_shell_init(void)
{
    kern_spawn("shell", shell_task, NULL);
}
```

Shell 任务通过 `kern_spawn` 启动，作为第一个非 idle 任务运行。启动后显示 `Xeros> ` 提示符等待输入。

### 命令集（完整列表）

Shell 目前支持 **30+ 条命令**（Phase 3 从 18 条扩充至 33 条），按功能分为 6 大类：

#### 文件系统操作（8 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `ls` | `ls [path]` | 列出目录内容 |
| `cd` | `cd <dir>` | 切换当前目录 |
| `pwd` | `pwd` | 打印当前路径 |
| `cat` | `cat <file>` | 显示文件内容 |
| `echo` | `echo <text>` | 输出文本（可重定向 `>` / `>>`） |
| `touch` | `touch <file>` | 创建空文件 |
| `rm` | `rm <file>` | 删除文件 |
| `mkdir` | `mkdir <dir>` | 创建目录 |

#### 任务与内核信息（6 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `ps` | `ps` | 列出所有任务（PID / STATE / NAME / STACK） |
| `top` | `top` | 实时任务监控（循环 `ps` + 2s 间隔，任意键退出） |
| `free` | `free` | 堆内存统计 |
| `mem` | `mem` | 堆内存统计（`free` 命令的别名） |
| `uname` | `uname` | 内核名称/版本 |
| `stats` | `stats` | 内核统计信息 |

#### 调试诊断（4 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `log` | `log [level]` | 查看或设置日志级别（0-3） |
| `debug` | `debug <on\|off> [module]` | 模块调试开关 |
| `hexdump` | `hexdump <path>` | 十六进制文件转储 |
| `history` | `history` | 显示命令历史（环形缓冲区，最多 16 条） |

#### 系统控制（4 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `reboot` | `reboot` | 重启设备 |
| `bootloader` | `bootloader` | 进入 OTA 升级模式（切换启动分区 + 重启） |
| `factory` | `factory` | **恢复出厂设置**（二次确认后清除 NVS + 重启） |
| `version` | `version` | 固件版本、平台、编译时间 |

#### 参数配置（3 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `param list` | `param list` | 列出所有可配置参数及当前值 |
| `param get <name>` | `param get brightness` | 读取单个参数值 |
| `param set <name> <value>` | `param set brightness 128` | 设置参数值（通过 sysfs 写操作，自带范围校验） |

#### 实时监测（3 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `scope add <path>` | `scope add /sys/brightness` | 注册观测变量（最多 8 个） |
| `scope start [ms]` | `scope start 500` | 开始周期 CSV 输出 |
| `scope stop` | `scope stop` | 停止监测 |

#### 运行时控制（3 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `mode [set <n>]` | `mode set 1` | 运行模式（0=manual/1=auto/2=calibrate/3=estop） |
| `ctrl <cmd>` | `ctrl start` | 控制算法启停（stop/start/reset） |
| `io <get\|set> <pin> [val]` | `io get 25` | GPIO 引脚调试读写 |

#### 辅助工具（3 条）

| 命令 | 语法 | 说明 |
|------|------|------|
| `info` | `info` | 设备汇总信息（uname + free + df + date） |
| `date` | `date` | 显示运行时间 |
| `help` | `help [cmd]` | 列出所有命令，或查看单条命令详细说明 |

### 工作目录

Shell 维护一个 `cwd` 变量（VFS 的工作目录 inode）。`cd` 命令通过 `kern_vfs_resolve(cwd, path)` 更新 cwd。所有相对路径（如 `ls dev`）相对于 cwd 解析。

### 命令解析流程

*📄 Source: [kern_shell.c](../../src/kernel/kern_shell.c#L400-L500)*

```
读入一行 → 分词 (按空格分割)
    ├─ 识别命令名 (第一个词)
    ├─ 识别参数 (剩余词)
    ├─ 识别特殊重定向 (>, >>)
    └─ 分派命令
```

#### 中文伪代码拆解

```
Shell 主循环：

while (正在运行) {
    显示提示符 "Xeros> "
    读取输入行 (调用 sys_read(fd_ttyS0, line_buf, ...))

    跳过前导空白
    提取命令名 (第一个空格前的词)

    解析参数 (剩余空格分隔的词)
    按命令名分派：
        case "ls":    遍历目录调用 readdir
        case "cat":   打开文件读取逐行输出
        case "cd":    路径解析更新 cwd
        case "ps":    遍历任务链表输出
        case "top":   循环 ps + 2s 间隔
        ...
}
```

### 命令注册机制

*📄 Source: [kern_shell_cmds.h](../../src/kernel/kern_shell_cmds.h)*

命令采用**表驱动**设计：

```c
typedef struct {
    const char           *name;      // 命令名（精确匹配）
    shell_cmd_handler_t   handler;   // 处理函数
    const char           *help;      // 帮助文本（单行）
} shell_cmd_t;
```

- **内置命令表**：`g_builtin_cmds[]` 静态数组，最多 48 条（`SHELL_MAX_BUILTIN_CMDS`）
- **动态注册**：`kern_shell_register_cmd()` 支持运行时注册扩展命令（最多 8 条）
- **查找优先级**：先查动态注册表，再查内置表

---

## Scope — 实时数据监测引擎（Phase 3 新增）

### 概述

Scope 是一个轻量级实时数据监测子系统，允许在 Shell 中注册 `/proc/` 或 `/sys/` 路径作为观测变量，并按固定周期以 CSV 格式输出到串口。

### 数据结构

*📄 Source: [kern_shell_cmds_internal.h](../../src/kernel/kern_shell_cmds_internal.h)*

```c
#define SCOPE_MAX_VARS 8

typedef struct {
    char   path[KERN_PATH_MAX];
    bool   active;
} scope_var_t;

// 全局状态
scope_var_t g_scope_vars[SCOPE_MAX_VARS];  // 观测变量数组
int         g_scope_count;                  // 已注册变量数
bool        g_scope_running;                // 是否正在运行
int         g_scope_period_ms;              // 采样周期（ms）
uint64_t    g_scope_last_tick;              // 上次采样时间戳
```

### 架构注入

*📄 Source: [kern_shell.c](../../src/kernel/kern_shell.c#L88-L89)*

```c
for (;;) {
    /* Phase 3: scope tick — 非阻塞周期数据输出 */
    scope_tick(tty);

    char ch;
    ssize_t n = kern_read(tty, &ch, 1);
    // ...
}
```

`scope_tick()` 在 Shell 主循环的 `kern_read()` 阻塞之前执行，利用 `esp_timer_get_time()` 判断距上次采样是否已过 `period_ms`。非阻塞设计——当没有数据需要输出时立即返回，不影响 Shell 交互响应速度。

### 使用示例

```
Xeros> scope add /sys/brightness
OK
Xeros> scope add /proc/uptime
OK
Xeros> scope start 1000
scope started
128,1234567
128,1235568
128,1236570
Xeros> scope stop
scope stopped
```

---

## 输入/输出

Shell 通过 `/dev/ttyS0` 设备文件读写串口：

- `sys_read(fd_ttyS0, &ch, 1)` — 逐字符读取，阻塞直到有新字符（scope_tick 在每个阻塞周期之前插入）
- `sys_write(fd_ttyS0, buf, len)` — 输出响应文本

输出经过 ANSI 转义序列处理（`\033[2J` 清屏、光标定位等）。

---

## 命令实现模式

### 无参数命令（直接执行）

```c
static void cmd_free(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[128];
    // 读取内存统计...
    snprintf(line, sizeof(line), "Free heap: %" PRIu32, free_heap);
    sh_println(tty, line);
}
```

### 子命令模式（param / scope / io）

```c
static void cmd_param(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    if (argc < 2) { sh_println(tty, "Usage: ..."); return; }
    if (strcmp(argv[1], "list") == 0) { /* ... */ }
    else if (strcmp(argv[1], "get") == 0) { /* ... */ }
    else if (strcmp(argv[1], "set") == 0) { /* ... */ }
}
```

### 危险操作二次确认（factory）

```c
static void cmd_factory(kern_fd_t tty, ...) {
    sh_println(tty, "WARNING: This will erase ALL settings and reboot.");
    sh_print(tty, "Type 'yes' to confirm: ");
    char confirm[8]; kern_read(tty, confirm, ...);
    if (strcmp(confirm, "yes") != 0) { sh_println(tty, "Aborted."); return; }
    nvs_flash_erase();  // 清除 NVS
    esp_restart();       // 重启
}
```

---

## 与其他组件的关系

- **kern_vfs**：所有文件操作命令（ls/cat/cd/touch/rm/mkdir）全部通过 VFS API
- **kern_task**：`ps`/`top` 命令遍历调度器任务链表
- **kern_sysfs**：`param`/`log`/`mode`/`ctrl` 命令通过 sysfs 节点读写参数
- **kern_init**：`stats` 命令查询内核统计信息
- **kern_devices**：通过 `/dev/ttyS0` 读写串口
- **kern_procfs**：`scope` 可监测任意 `/proc/` 和 `/sys/` 路径

---

> **See Also:** [VFS](kern-vfs.md) | [物理设备](kern-devices.md) | [调度器](kern-task.md) | [初始化](kern-init.md) | [/proc 与 /sys](kern-procfs-sysfs.md)
