# Shell 命令实现（kern_shell_cmds）

> **Parent:** [内核子系统总览](index.md) | **Related:** [内核 Shell](kern-shell.md), [Shell 解析器](kern-shell-parser.md)

## 概述

`kern_shell_cmds` 是 Xeros Shell 的**命令实现层**，包含全部内置命令的处理函数和命令注册表。每条命令通过统一的 `kern_shell_cmd_t` 结构注册。当前实现为静态命令表，不支持运行时动态添加。

---

## 命令注册表

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L883-L926)*

```c
typedef struct {
    const char           *name;
    kern_shell_cmd_handler_t handler;
    const char           *help;
} kern_shell_cmd_t;
```

命令表是一个静态数组，由 `kern_shell_lookup_cmd()` 线性遍历查找：

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L883-L926)*

```c
static const kern_shell_cmd_t g_builtin_cmds[] = {
    /* ── 文件系统命令 ── */
    { "ls",      cmd_ls,       "list directory" },
    { "cd",      cmd_cd,       "change directory" },
    { "pwd",     cmd_pwd,      "print working directory" },
    { "cat",     cmd_cat,      "read file" },
    { "cp",      cmd_cp,       "copy file" },
    { "rm",      cmd_rm,       "remove file/dir" },
    { "mkdir",   cmd_mkdir,    "create directory" },
    { "touch",   cmd_touch,    "create empty file" },
    { "echo",    cmd_echo,     "print text (echo text > file to write)" },

    /* ── 系统命令 ── */
    { "ps",      cmd_ps,       "list tasks" },
    { "reboot",  cmd_reboot,   "restart device" },
    { "help",    cmd_help,     "this help" },

    /* ── 新增命令 ── */
    { "free",    cmd_free,     "show heap memory" },
    { "kill",    cmd_kill,     "terminate task <pid>" },
    { "uname",   cmd_uname,    "print system info" },
    { "df",      cmd_df,       "show VFS usage" },
    { "clear",   cmd_clear,    "clear screen" },
    { "history", cmd_history,  "show command history" },
    { "date",    cmd_date,     "show uptime" },
    { "hexdump", cmd_hexdump,  "hex dump <path>" },

    /* ── Phase 3 新增命令 ── */
    { "top",       cmd_top,       "real-time task monitor" },
    { "mem",       cmd_free,      "show heap memory (alias: free)" },
    { "log",       cmd_log,       "view/set log level" },
    { "param",     cmd_param,     "config parameters (list/get/set/save/load)" },
    { "bootloader",cmd_bootloader,"enter OTA bootloader mode" },
    { "factory",   cmd_factory,   "factory reset (DANGER!)" },
    { "version",   cmd_version,   "firmware & hardware info" },
    { "scope",     cmd_scope,     "real-time data scope (add/start/stop)" },
    { "mode",      cmd_mode,      "view/set run mode" },
    { "ctrl",      cmd_ctrl,      "control algorithm start/stop/reset" },
    { "info",      cmd_info,      "device summary info" },
    { "io",        cmd_io,        "GPIO debug (get/set <pin> [value])" },

    /* ── App 配置命令 ── */
    { "dskey",     cmd_dskey,     "set/view DeepSeek API key" },
};
```

---

## 命令实现示例

### ps — 列出所有任务

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L111-L137)*

```c
static void cmd_ps(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;

    kern_task_t *task = kern_task_list_head();
    char line[100];

    kern_shell_println(tty, "PID  STATE     NAME          STACK");

    while (task != NULL) {
        const char *state_str;
        switch (task->state) {
        case KERN_TASK_READY:    state_str = "READY    "; break;
        case KERN_TASK_RUNNING:  state_str = "RUNNING  "; break;
        case KERN_TASK_SLEEPING: state_str = "SLEEP    "; break;
        case KERN_TASK_BLOCKED:  state_str = "BLOCKED  "; break;
        case KERN_TASK_ZOMBIE:   state_str = "ZOMBIE   "; break;
        default:                 state_str = "?????    "; break;
        }

        snprintf(line, sizeof(line), "%-4d %s %-12s %zu/%zu",
                 (int)task->pid, state_str, task->name,
                 kern_task_stack_usage(task), task->stack_size);
        kern_shell_println(tty, line);

        task = task->next;
    }
}
```

#### 中文伪代码拆解

```
函数 命令_ps(终端, 参数个数, 参数数组, 当前目录, 目录大小) {
    忽略所有参数（ps 不需要参数）

    获取任务链表头指针
    打印表头: "PID  STATE     NAME          STACK"

    while (遍历所有任务) {
        根据任务状态枚举，选择对应的状态字符串

        格式化一行: PID | 状态 | 名称 | 已用栈/总栈大小
        打印这一行

        移动到下一个任务
    }
}
```

**核心思想**：遍历内核任务链表，将每个任务的状态、名称和栈使用情况格式化为表格输出。

---

### scope — 实时数据监测引擎

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L756-L780)*

```c
static void cmd_scope(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: scope <add|start|stop> [args]"); return; }

    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        if (g_scope_count >= SCOPE_MAX_VARS) {
            kern_shell_println(tty, "scope: max variables reached"); return;
        }
        strncpy(g_scope_vars[g_scope_count].path, argv[2], KERN_PATH_MAX - 1);
        g_scope_vars[g_scope_count].active = true;
        g_scope_count++;
        kern_shell_println(tty, "OK");
    } else if (strcmp(argv[1], "start") == 0) {
        if (argc >= 3) g_scope_period_ms = atoi(argv[2]);
        g_scope_running = true;
        g_scope_last_tick = 0;
        kern_shell_println(tty, "scope started");
    } else if (strcmp(argv[1], "stop") == 0) {
        g_scope_running = false;
        kern_shell_println(tty, "scope stopped");
    } else {
        kern_shell_println(tty, "scope: unknown sub-command");
    }
}
```

**功能**：注册 `/proc/` 或 `/sys/` 路径作为观测变量，按固定周期以 CSV 格式输出到串口。`scope add` 注册路径，`scope start [ms]` 启动周期输出，`scope stop` 停止。最多支持 8 个变量。`kern_shell_scope_tick()` 在 Shell 主循环中非阻塞执行实际采样。

---

## 命令历史

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L72-L94)*

```c
#define HISTORY_SIZE 16

static char g_history[HISTORY_SIZE][128];
static int  g_hist_count = 0;
static int  g_hist_index = 0;

void shell_history_add(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') return;
    /* 去重：重复命令不重复记录 */
    int last = (g_hist_count > 0) ? ((g_hist_count - 1) % HISTORY_SIZE) : -1;
    if (last >= 0 && strcmp(g_history[last], cmd) == 0) return;

    strncpy(g_history[g_hist_index], cmd, 127);
    g_history[g_hist_index][127] = '\0';
    g_hist_index = (g_hist_index + 1) % HISTORY_SIZE;
    if (g_hist_count < HISTORY_SIZE) g_hist_count++;
}
```

#### 中文伪代码拆解

```
常量 历史大小 = 16
字符数组 历史[16][128]
整数 历史条数 = 0
整数 写入位置 = 0

函数 添加历史(命令) {
    if (命令为空) return

    // 去重检查
    最后一条索引 = (历史条数 > 0) ? (历史条数 - 1) % 16 : -1
    if (最后一条存在 且 最后一条 == 当前命令) return

    复制命令到历史[写入位置]
    写入位置 = (写入位置 + 1) % 16    // 环形缓冲区
    if (历史条数 < 16) 历史条数++
}
```

**核心思想**：使用环形缓冲区存储最近 16 条命令，支持上下键回溯。连续输入相同命令时只保留一条，避免历史被重复命令占满。

---

## 输出辅助函数

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L41-L52)*

```c
void kern_shell_print(kern_fd_t tty, const char *msg)
{
    if (tty >= 0 && msg != NULL) {
        kern_write(tty, msg, strlen(msg));
    }
}

void kern_shell_println(kern_fd_t tty, const char *msg)
{
    kern_shell_print(tty, msg);
    kern_shell_print(tty, "\r\n");
}
```

所有命令统一通过 `kern_shell_print`/`kern_shell_println` 输出，底层经过 VFS 的 `/dev/ttyS0` 写入串口。

---

---

> **See Also:** [内核 Shell](kern-shell.md) | [Shell 解析器](kern-shell-parser.md) | [抢占式调度器](kern-task.md)
