# IPC 进程间通信（Kern IPC）

> **Parent:** [内核总览](index.md) | **Related:** [调度器](kern-task.md), [VFS](kern-vfs.md), [系统调用](kern-syscall.md)

## 概述

`kern_ipc` 实现了 Xeros 内核中任务间通信的两种机制：

- **匿名管道（Anonymous Pipe）**：环形缓冲区，固定 512 字节容量，生产者-消费者模型
- **命名消息队列（Named Message Queue）**：通过 VFS `/tmp/xxx` 暴露，支持 fan-out 多消费者

两种机制都通过 VFS 统一接口访问（`sys_open` → `sys_read` / `sys_write`）。

---

## 关键概念

### 匿名管道

*📄 Source: [kern_ipc.c](../../src/kernel/kern_ipc.c#L28-L55)*

```c
#define PIPEBUF_SIZE 512  /* 固定 512 字节环形缓冲区 */

typedef struct kern_pipe {
    unsigned char  buf[PIPEBUF_SIZE];
    volatile int   head;          /* 写指针 */
    volatile int   tail;          /* 读指针 */
    volatile int   total;         /* 当前字节数 */
    volatile bool  closed_read;   /* 读端已关闭 */
    volatile bool  closed_write;  /* 写端已关闭 */
    kern_pid_t     writer_pid;    /* 写入者 PID */
    kern_pid_t     reader_pid;    /* 读取者 PID */
} kern_pipe_t;
```

#### 中文伪代码拆解

```
结构体 管道 {
    环形缓冲区[512字节]
    写指针     写入位置
    读指针     读取位置
    当前字节数  有效数据量
    读端已关闭标记
    写端已关闭标记
    写入者PID   创建管道的任务
    读取者PID   接收数据的任务
}

管道工作原理：
    head 和 tail 在 512 字节内循环
    空管道:  head == tail 且 total == 0
    满管道:  head == tail 且 total == 512

    write 操作：
        将数据从 head 位置开始写入
        head = (head + n) % 512
        total += n

    read 操作：
        从 tail 位置开始读取
        tail = (tail + n) % 512
        total -= n
```

### 阻塞读/写机制

*📄 Source: [kern_ipc.c](../../src/kernel/kern_ipc.c#L75-L130)*

```c
/* 管道读（阻塞语义） */
static int pipe_read(kern_file_t *file, unsigned char *buf, uint32_t count)
{
    kern_pipe_t *pipe = (kern_pipe_t *)file->f_inode->i_pipe;

    /* 如果管道为空且写端未关闭 → 阻塞等待 */
    while (pipe->total == 0 && !pipe->closed_write) {
        current_task->blocked_on = pipe->writer_pid;
        sys_yield();  /* 让出 CPU */
    }

    /* 管道为空且写端已关闭 → EOF（返回0） */
    if (pipe->total == 0 && pipe->closed_write) return 0;

    /* 从环形缓冲区逐字节副本到用户 buf */
    // ...
    return bytes_read;
}
```

#### 中文伪代码拆解

```
管道 read 语义：

    while (当前没有数据 且 写端未关闭) {
        阻塞自己(等待写入者PID)
        sys_yield()  // 让出CPU，等待写入者产生数据
    }

    if (没有数据 且 写端已关闭) return 0  // EOF

    从环形缓冲读数据（部分拷贝）
    return 读取的字节数

管道 write 语义：

    while (缓冲区满 且 读端未关闭) {
        阻塞自己(等待读取者PID)
        sys_yield()  // 让出CPU，等待读取者消化数据
    }

    if (读端已关闭) return -32  // EPIPE

    写入环形缓冲（可能部分写入）
    return 写入的字节数
```

### 管道限制和设计选择

- **固定 512 字节**：这是典型的 `PIPE_BUF` 大小（POSIX 最小值），确保原子写入
- **阻塞语义**：写入者在管道满时让出 CPU，读取者在管道空时让出。因为 Xeros 是协作式调度，这不需要锁
- **部分读写**：如果请求 count > 可用容量，读/写返回实际处理的字节数（不会一直等到满足全部需求）

### 命名消息队列

*📄 Source: [kern_ipc.c](../../src/kernel/kern_ipc.c#L280-L450)*

命名消息队列通过 VFS `/tmp/xxx` 暴露：

```c
int kern_mq_open(const char *name);    /* 在 /tmp 下创建命名管道 */
int kern_mq_send(int fd, const void *msg, uint32_t len);  /* 发送消息 */
int kern_mq_recv(int fd, void *msg, uint32_t max_len);    /* 接收消息 */
int kern_mq_close(int fd);             /* 关闭连接 */
```

#### 中文伪代码拆解

```
命名消息队列与匿名管道的区别：

匿名管道：
    ┌─────┐  512B buf  ┌─────┐
    │写者 │───→ tail / head ←│读者 │
    └─────┘              └─────┘
    特点：点对点、创建时绑定两个 PID

命名消息队列：
    ┌─────┐     ┌─────────┐     ┌─────┐
    │生产者│──→ │ /tmp/mq │ ←── │消费者1│
    └─────┘     │ (VFS)   │ ←── │消费者2│
                └─────────┘     └─────┘
    特点：多对多、通过文件路径寻址、复用管道实现

实现思路：
    kern_mq_open("/tmp/myqueue")  → kern_vfs_create 命名管道 inode
    kern_mq_send(fd, data, len)   → sys_write(fd, data, len) → pipe_write
    kern_mq_recv(fd, buf, max)    → sys_read(fd, buf, max)   → pipe_read
```

---

## 使用场景

| 场景 | 机制 | 示例 |
|------|------|------|
| UI 按键分发 | 匿名管道 | 按键任务 `sys_write(pipe_fd, key_event)` → UI 任务 `sys_read(pipe_fd, ...)` |
| Shell 命令输出 | 匿名管道 | Shell task 执行 `cat /proc/tasks` → stdout 通过 pipe 到串口 task |
| 跨任务通知 | 命名消息队列 | WiFi 任务完成扫描 → mq_send → BT 任务收到通知 |
| 设备数据流 | 命名消息队列 | 串口输入 task → mq_send → 解析器 task |

---

## 与其他组件的关系

- **kern_vfs**：匿名管道的 fd 存储在 `fd_table` 中；命名 MQ 通过 `/tmp/` 路径注册
- **kern_task**：`blocked_on` 字段用于阻塞等待另一端的任务
- **kern_syscall**：`sys_read` / `sys_write` 路由到管道的 `file_ops`
- **kern_shell**：管道用于 Shell 命令输出重定向

---

> **See Also:** [调度器](kern-task.md) | [VFS](kern-vfs.md) | [系统调用](kern-syscall.md)
