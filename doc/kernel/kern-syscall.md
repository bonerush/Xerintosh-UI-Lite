# 系统调用接口（Kern Syscall）

> **Parent:** [内核总览](index.md) | **Related:** [VFS](kern-vfs.md), [调度器](kern-task.md), [IPC](kern-ipc.md)

## 概述

`kern_syscall` 实现了 Xeros 内核的统一系统调用分发器。"系统调用"并不需要真实的 CPU 特权级切换（内核和任务代码运行在同一个 FreeRTOS 线程内），而是作为统一的函数调用分发表，将任务请求路由到对应的内核模块。

---

## 关键概念

### 系统调用编号

*📄 Source: [kern_syscall.h](../../src/kernel/kern_syscall.h#L19-L45)*

```c
typedef enum {
    SYS_YIELD      = 1,   /* 让出 CPU */
    SYS_EXIT       = 2,   /* 退出当前任务 */
    SYS_SLEEP      = 3,   /* 睡眠 N 毫秒 */
    SYS_GETPID     = 4,   /* 获取当前 PID */

    SYS_OPEN       = 10,  /* 打开文件/设备/管道 */
    SYS_CLOSE      = 11,  /* 关闭文件描述符 */
    SYS_READ       = 12,  /* 读取数据 */
    SYS_WRITE      = 13,  /* 写入数据 */
    SYS_IOCTL      = 14,  /* 设备控制 */
    SYS_LSEEK      = 15,  /* 重定位文件偏移 */
    SYS_MKDIR      = 16,  /* 创建目录 */
    SYS_UNLINK     = 17,  /* 删除文件 */
    SYS_CREATE     = 18,  /* 创建文件 */

    SYS_MQ_OPEN    = 30,  /* 打开命名消息队列 */
    SYS_MQ_SEND    = 31,  /* 发送消息 */
    SYS_MQ_RECV    = 32,  /* 接收消息 */
    SYS_MQ_CLOSE   = 33,  /* 关闭消息队列 */
} kern_syscall_no_t;
```

**编号分组**：1-9 任务管理，10-19 文件操作，30-39 IPC。保留扩展空间。

### 分发表（Dispatch Table）

*📄 Source: [kern_syscall.c](../../src/kernel/kern_syscall.c#L16-L36)*

```c
/* typedef int (*kern_syscall_handler_t)(uint32_t a1, a2, a3, a4); */
static kern_syscall_handler_t syscall_table[] = {
    [SYS_YIELD]  = sys_yield_handler,
    [SYS_EXIT]   = sys_exit_handler,
    [SYS_SLEEP]  = sys_sleep_handler,
    [SYS_GETPID] = sys_getpid_handler,
    [SYS_OPEN]   = sys_open_handler,
    [SYS_CLOSE]  = sys_close_handler,
    [SYS_READ]   = sys_read_handler,
    [SYS_WRITE]  = sys_write_handler,
    [SYS_IOCTL]  = sys_ioctl_handler,
    [SYS_LSEEK]  = sys_lseek_handler,
    // ...
};
```

系统中只有一个 `syscall_table[]`，索引即 `SYS_*` 枚举值。不存在的调用号对应 NULL，返回 `KERN_EINVAL`。

### 分发器入口

*📄 Source: [kern_syscall.c](../../src/kernel/kern_syscall.c#L40-L55)*

```c
int kern_syscall(uint32_t syscall_no, uint32_t a1, uint32_t a2,
                  uint32_t a3, uint32_t a4)
{
    if (syscall_no >= sizeof(syscall_table) / sizeof(syscall_table[0])) {
        return KERN_EINVAL;
    }
    kern_syscall_handler_t handler = syscall_table[syscall_no];
    if (handler == NULL) {
        return KERN_EINVAL;
    }
    return handler(a1, a2, a3, a4);
}
```

#### 中文伪代码拆解

```
函数 内核系统调用(调用号, 参数1, 参数2, 参数3, 参数4) {
    if (调用号超出分发表范围) return 参数无效

    处理器 = 分发表[调用号]
    if (处理器 == NULL) return 参数无效

    返回 处理器(参数1, 参数2, 参数3, 参数4)
}
```

### 用户态封装

*📄 Source: [kern_syscall.h](../../src/kernel/kern_syscall.h#L50-L78)*

```c
static inline int sys_yield(void) { return kern_syscall(SYS_YIELD, 0,0,0,0); }
static inline int sys_exit(int ret) { return kern_syscall(SYS_EXIT, (uint32_t)ret,0,0,0); }
static inline int sys_sleep_ms(uint32_t ms) { return kern_syscall(SYS_SLEEP, ms,0,0,0); }
static inline int sys_getpid(void) { return kern_syscall(SYS_GETPID, 0,0,0,0); }

static inline int sys_open(const char *path, int mode) {
    return kern_syscall(SYS_OPEN, (uint32_t)(uintptr_t)path, (uint32_t)mode, 0, 0);
}
static inline int sys_read(int fd, unsigned char *buf, uint32_t cnt) {
    return kern_syscall(SYS_READ, (uint32_t)fd, (uint32_t)(uintptr_t)buf, cnt, 0);
}
static inline int sys_write(int fd, const unsigned char *buf, uint32_t cnt) {
    return kern_syscall(SYS_WRITE, (uint32_t)fd, (uint32_t)(uintptr_t)buf, cnt, 0);
}
// ...
```

用户态任务调用 `sys_open("/dev/input0", O_RDONLY)` 等价于内核内部 `kern_vfs_open("/dev/input0", O_RDONLY)`。这个封装层的存在是为了：

1. **统一调用点**：可以在 dispatch 层添加权限检查、日志记录、调用追踪
2. **未来扩展**：如果将来引入真正的特权级分离，只需修改 `kern_syscall()` 内部触发 CPU 异常即可
3. **调试友好**：可以在 dispatch 处下断点捕获所有系统调用

### 处理函数示例

*📄 Source: [kern_syscall.c](../../src/kernel/kern_syscall.c#L60-L78)*

```c
static int sys_open_handler(uint32_t path_ptr, uint32_t mode, uint32_t a3, uint32_t a4)
{
    const char *path = (const char *)(uintptr_t)path_ptr;
    return kern_vfs_open(path, (int)mode);
}

static int sys_exit_handler(uint32_t retval, uint32_t a2, uint32_t a3, uint32_t a4)
{
    kern_task_exit(current_task, (int)retval);
    kern_schedule();  /* 立即触发调度 */
    return 0;         /* 实际上不会到达 */
}
```

---

## 调用流程汇总

```
 应用程序代码:
     int fd = sys_open("/dev/fb0", O_WRONLY);
       │
       ├→ 行内函数封装（sys_open 宏）
       │     └→ kern_syscall(SYS_OPEN, "/dev/fb0", O_WRONLY, 0, 0)
       │           ├→ 分发表查找 syscall_table[10]
       │           ├→ 找到 sys_open_handler
       │           └→ 调用 handler(path, mode, ...)
       │                 └→ kern_vfs_open("/dev/fb0", O_WRONLY)
       │                       ├→ 路径解析 ✓
       │                       ├→ file 结构分配 ✓
       │                       ├→ i_fops.open(inode, file) ✓
       │                       └→ fd 在 fd_table 中分配 ✓
       │
       └→ 返回 fd (例如 3)
```

---

## 设计要点

- **无中断/异常触发**：目前 syscall 就是普通函数调用，因为内核和任务共享同一个线程栈
- **统一 4 个 uint32_t 参数**：所有 handler 签名一致，用 `uintptr_t` 在指针和整数间转换
- **静态分发表**：编译时确定，无动态注册，保证零开销

---

> **See Also:** [VFS](kern-vfs.md) | [调度器](kern-task.md) | [IPC](kern-ipc.md)
