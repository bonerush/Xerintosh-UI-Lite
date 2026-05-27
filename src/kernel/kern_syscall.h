/**
 * @file   kern_syscall.h
 * @brief  Xeros 系统调用接口头文件
 * @details 定义 syscall 编号、统一分发器 kern_syscall() 和用户态封装函数。
 *          所有用户态任务应通过 sys_* 封装访问内核服务，而非直接调用 kern_*。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SYSCALL_H
#define KERN_SYSCALL_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ Syscall 编号 ═══ */

#define SYS_OPEN     0   /* 打开文件 */
#define SYS_CLOSE    1   /* 关闭文件 */
#define SYS_READ     2   /* 读取数据 */
#define SYS_WRITE    3   /* 写入数据 */
#define SYS_IOCTL    4   /* 设备控制 */
#define SYS_YIELD    5   /* 让出 CPU */
#define SYS_SLEEP    6   /* 休眠 */
#define SYS_EXIT     7   /* 退出当前任务 */
#define SYS_PIPE     8   /* 创建匿名管道 */
#define SYS_SPAWN    9   /* 创建新任务 */

#define SYS_MAX      10   /* syscall 总数 */

/* ═══ Syscall 分发器 ═══ */

/**
 * @brief  系统调用统一入口
 * @param  num  系统调用编号（SYS_OPEN ~ SYS_SPAWN）
 * @param  arg1 参数 1（语义因调用而异）
 * @param  arg2 参数 2
 * @param  arg3 参数 3
 * @param  arg4 参数 4
 * @return >= 0 成功，< 0 为错误码（void 调用返回 0 表示成功）
 * @note   固定 4 参数设计避免嵌入式平台 varargs 开销
 */
long kern_syscall(long num, long arg1, long arg2, long arg3, long arg4);

/* ═══ 用户态封装函数 ═══ */

/**
 * @brief  打开文件（用户态封装）
 * @param  path  文件路径
 * @param  flags 打开标志（KERN_O_RDONLY / KERN_O_WRONLY / KERN_O_RDWR）
 * @return >= 0 为文件描述符，< 0 为错误码
 */
kern_fd_t sys_open(const char *path, unsigned int flags);

/**
 * @brief  关闭文件描述符（用户态封装）
 * @param  fd 文件描述符
 * @return KERN_OK 成功，< 0 为错误码
 */
int sys_close(kern_fd_t fd);

/**
 * @brief  从文件描述符读取数据（用户态封装）
 * @param  fd  文件描述符
 * @param  buf 输出缓冲区
 * @param  len 读取长度
 * @return > 0 为实际读取字节数，0 为 EOF，< 0 为错误码
 */
ssize_t sys_read(kern_fd_t fd, void *buf, size_t len);

/**
 * @brief  向文件描述符写入数据（用户态封装）
 * @param  fd  文件描述符
 * @param  buf 输入缓冲区
 * @param  len 写入长度
 * @return > 0 为实际写入字节数，< 0 为错误码
 */
ssize_t sys_write(kern_fd_t fd, const void *buf, size_t len);

/**
 * @brief  对文件描述符执行设备控制操作（用户态封装）
 * @param  fd  文件描述符
 * @param  cmd 控制命令
 * @param  arg 可选参数
 * @return KERN_OK 成功，< 0 为错误码
 */
int sys_ioctl(kern_fd_t fd, unsigned int cmd, unsigned long arg);

/**
 * @brief  主动让出 CPU（用户态封装）
 */
void sys_yield(void);

/**
 * @brief  休眠指定的毫秒数（用户态封装）
 * @param  ms 休眠时间（毫秒）
 */
void sys_sleep_ms(uint32_t ms);

/**
 * @brief  终止当前任务（用户态封装）
 */
void sys_exit(void);

/**
 * @brief  创建匿名管道（用户态封装）
 * @param  fds 输出数组，fds[0] 为读端，fds[1] 为写端
 * @return KERN_OK 成功，< 0 为错误码
 */
int sys_pipe(kern_fd_t fds[2]);

/**
 * @brief  创建并启动新任务（用户态封装）
 * @param  name      任务名称
 * @param  entry     任务入口函数
 * @param  arg       入口参数（可为 NULL）
 * @param  stack_min 初始栈大小（0 表示使用默认值）
 * @return >= 0 为 PID，< 0 为错误码
 */
kern_pid_t sys_spawn(const char *name, void (*entry)(void *arg),
                     void *arg, size_t stack_min);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SYSCALL_H */
