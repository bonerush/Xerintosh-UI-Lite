# 内核 Shell（Kern Shell）

> **Parent:** [内核总览](index.md) | **Related:** [VFS](kern-vfs.md), [物理设备](kern-devices.md), [调度器](kern-task.md)

## 概述

`kern_shell` 实现了一个通过 **USB 串口** 交互的微型命令行解释器。它作为独立的 Xeros 任务运行，读取 `/dev/ttyS0` 的输入字符，解析命令，执行相应操作，结果输出到串口。

这是一个"活的内核调试器"——允许在运行时查看任务列表、浏览文件系统、读写 `/sys` 节点、甚至创建/删除文件。

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

### 命令集

| 命令 | 语法 | 说明 |
|------|------|------|
| `help` | `help` | 列出所有命令 |
| `ls` | `ls [path]` | 列出目录内容 |
| `cd` | `cd <dir>` | 切换当前目录 |
| `pwd` | `pwd` | 打印当前路径 |
| `ps` | `ps` | 列出所有任务 |
| `cat` | `cat <file>` | 显示文件内容 |
| `echo` | `echo <text>` | 输出文本（可重定向） |
| `touch` | `touch <file>` | 创建空文件 |
| `rm` | `rm <file>` | 删除文件 |
| `mkdir` | `mkdir <dir>` | 创建目录 |
| `stats` | `stats` | 显示内核统计信息 |
| `reboot` | `reboot` | 重启设备（ESP.restart） |
| `clear` | `clear` | 清除屏幕 |

### 工作目录

Shell 维护一个 `cwd` 变量（VFS 的工作目录 inode）。`cd` 命令通过 `kern_vfs_resolve(cwd, path)` 更新 cwd。所有相对路径（如 `ls dev`）相对于 cwd 解析。

### 命令解析流程

*📄 Source: [kern_shell.c](../../src/kernel/kern_shell.c#L400-L500)*

```
读入一行 → 分词 (按空格分割)
    ├─ 识别命令名 (第一个词)
    ├─ 识别参数 (剩余词)
    ├─ 识别特殊重定向 (>, >>, |)
    └─ 分派命令
```

#### 中文伪代码拆解

```
Shell 命令解析循环：

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
        case "touch": 创建空文件
        case "echo":  输出参数到标准输出
        ...
}
```

### 输入/输出

Shell 通过 `/dev/ttyS0` 设备文件读写串口：

- `sys_read(fd_ttyS0, &ch, 1)` — 逐字符读取，阻塞直到有新字符
- `sys_write(fd_ttyS0, buf, len)` — 输出响应文本

输出经过 ANSI 转义序列处理（`\033[2J` 清屏、光标定位等）。

---

## 与其他组件的关系

- **kern_vfs**：所有文件操作命令（ls/cat/cd/touch/rm/mkdir）全部通过 VFS API
- **kern_task**：`ps` 命令遍历调度器任务链表
- **kern_init**：`stats` 命令查询内核统计信息
- **kern_devices**：通过 `/dev/ttyS0` 读写串口

---

> **See Also:** [VFS](kern-vfs.md) | [物理设备](kern-devices.md) | [调度器](kern-task.md) | [初始化](kern-init.md)
