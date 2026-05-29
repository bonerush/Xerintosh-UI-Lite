# Shell 命令实现（kern_shell_cmds）

> **Parent:** [内核子系统总览](index.md) | **Related:** [内核 Shell](kern-shell.md), [Shell 解析器](kern-shell-parser.md)

## 概述

`kern_shell_cmds` 是 Xeros Shell 的**命令实现层**，包含全部内置命令的处理函数和命令注册表。每条命令通过统一的 `kern_shell_cmd_t` 结构注册，支持动态添加扩展命令。

---

## 命令注册表

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L830-L880)*

```c
typedef struct {
    const char *name;
    cmd_handler_fn handler;
    const char *help;
} kern_shell_cmd_t;
```

命令表是一个静态数组，启动时由 `kern_shell_init()` 遍历注册到哈希表中：

```c
static const kern_shell_cmd_t g_builtin_cmds[] = {
    {"help",    cmd_help,    "显示帮助信息"},
    {"ps",      cmd_ps,      "列出所有任务"},
    {"top",     cmd_top,     "实时任务监控"},
    {"kill",    cmd_kill,    "终止指定 PID 的任务"},
    {"ls",      cmd_ls,      "列出目录内容"},
    {"cat",     cmd_cat,     "显示文件内容"},
    {"echo",    cmd_echo,    "回显文本"},
    {"cd",      cmd_cd,      "切换工作目录"},
    {"pwd",     cmd_pwd,     "显示当前目录"},
    {"mkdir",   cmd_mkdir,   "创建目录"},
    {"rm",      cmd_rm,      "删除文件或目录"},
    {"touch",   cmd_touch,   "创建空文件"},
    {"clear",   cmd_clear,   "清屏"},
    {"history", cmd_history, "显示命令历史"},
    {"scope",   cmd_scope,   "实时数据监测"},
    {"param",   cmd_param,   "读取/写入系统参数"},
    {"reboot",  cmd_reboot,  "重启系统"},
    {"free",    cmd_free,    "显示内存使用情况"},
    {"version", cmd_version, "显示版本信息"},
    /* ... 更多命令 ... */
};
```

---

## 命令实现示例

### ps — 列出所有任务

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L119-L145)*

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
                 task->pid, state_str, task->name,
                 task->stack_used, task->stack_size);
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

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L650-L720)*

```c
static void cmd_scope(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;

    if (argc < 2) {
        kern_shell_println(tty, "Usage: scope <period_ms> [var1] [var2] ...");
        return;
    }

    int period_ms = atoi(argv[1]);
    if (period_ms <= 0) {
        kern_shell_println(tty, "Invalid period");
        return;
    }

    /* 启动周期性 CSV 输出 */
    scope_start(tty, period_ms, argc - 2, (const char **)(argv + 2));
}
```

**功能**：以指定周期（毫秒）循环输出一组变量的 CSV 格式数据，直到用户按 Ctrl+C。常用于调试时观察内存、任务状态等实时变化。

---

## 命令历史

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L72-L115)*

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

*📄 Source: [kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L39-L52)*

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

## 动态命令注册

*📄 Source: [kern_shell_cmds_internal.h](../../src/kernel/kern_shell_cmds_internal.h)*

```c
int kern_shell_register_cmd(const char *name,
                            cmd_handler_fn handler,
                            const char *help);
```

通过 `kern_shell_register_cmd()` 可以在运行时动态添加新命令，无需修改 `kern_shell_cmds.c`。这为外设驱动或用户扩展提供了接口。

---

> **See Also:** [内核 Shell](kern-shell.md) | [Shell 解析器](kern-shell-parser.md) | [协作式调度器](kern-task.md)
