/**
 * @file   kern_syscall.c
 * @brief  Xeros 系统调用分发器实现
 * @details 实现统一的 kern_syscall() 分发入口和用户态封装函数。
 *          所有 sys_* 封装通过 kern_syscall() 间接调用内核函数，
 *          提供集中的调用追踪、参数校验和未来沙箱化基础。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_syscall.h"
#include "kern_vfs.h"
#include "kern_task.h"
#include "kern_ipc.h"
#include "kern_init.h"

/* ═══ Syscall 分发器 ═══ */

/**
 * @brief  系统调用统一入口
 * @note   所有用户态请求通过此函数路由到对应内核函数。
 *         arg1-arg4 为固定参数槽，语义因 syscall 而异（见各 case）。
 *         返回 long 以统一承载指针和整数类型。
 */
long kern_syscall(long num, long arg1, long arg2, long arg3, long arg4)
{
    switch (num) {
    case SYS_OPEN: {
        const char *path = (const char *)arg1;
        unsigned int flags = (unsigned int)arg2;
        return (long)kern_open(path, flags);
    }
    case SYS_CLOSE: {
        kern_fd_t fd = (kern_fd_t)arg1;
        return (long)kern_close(fd);
    }
    case SYS_READ: {
        kern_fd_t fd = (kern_fd_t)arg1;
        char *buf = (char *)arg2;
        size_t len = (size_t)arg3;
        return (long)kern_read(fd, buf, len);
    }
    case SYS_WRITE: {
        kern_fd_t fd = (kern_fd_t)arg1;
        const char *buf = (const char *)arg2;
        size_t len = (size_t)arg3;
        return (long)kern_write(fd, buf, len);
    }
    case SYS_IOCTL: {
        kern_fd_t fd = (kern_fd_t)arg1;
        unsigned int cmd = (unsigned int)arg2;
        unsigned long arg = (unsigned long)arg3;
        return (long)kern_ioctl(fd, cmd, arg);
    }
    case SYS_YIELD:
        kern_yield();
        return 0;

    case SYS_SLEEP:
        kern_sleep_ms((uint32_t)arg1);
        return 0;

    case SYS_EXIT:
        kern_exit();
        return 0;

    case SYS_PIPE: {
        kern_fd_t *fds = (kern_fd_t *)arg1;
        return (long)kern_pipe(fds);
    }
    case SYS_SPAWN: {
        const char *name = (const char *)arg1;
        void (*entry)(void *) = (void (*)(void *))arg2;
        void *entry_arg = (void *)arg3;
        size_t stack_min = (size_t)arg4;
        return (long)kern_spawn(name, entry, entry_arg, stack_min);
    }
    default:
        kern_log(KERN_LOG_WARN, "kern_syscall: unknown syscall number %ld", num);
        return (long)KERN_EINVAL;
    }
}

/* ═══ 用户态封装函数 ═══ */

kern_fd_t sys_open(const char *path, unsigned int flags)
{
    return (kern_fd_t)kern_syscall(SYS_OPEN, (long)path, (long)flags, 0, 0);
}

int sys_close(kern_fd_t fd)
{
    return (int)kern_syscall(SYS_CLOSE, (long)fd, 0, 0, 0);
}

ssize_t sys_read(kern_fd_t fd, void *buf, size_t len)
{
    return (ssize_t)kern_syscall(SYS_READ, (long)fd, (long)buf, (long)len, 0);
}

ssize_t sys_write(kern_fd_t fd, const void *buf, size_t len)
{
    return (ssize_t)kern_syscall(SYS_WRITE, (long)fd, (long)buf, (long)len, 0);
}

int sys_ioctl(kern_fd_t fd, unsigned int cmd, unsigned long arg)
{
    return (int)kern_syscall(SYS_IOCTL, (long)fd, (long)cmd, (long)arg, 0);
}

void sys_yield(void)
{
    kern_syscall(SYS_YIELD, 0, 0, 0, 0);
}

void sys_sleep_ms(uint32_t ms)
{
    kern_syscall(SYS_SLEEP, (long)ms, 0, 0, 0);
}

void sys_exit(void)
{
    kern_syscall(SYS_EXIT, 0, 0, 0, 0);
}

int sys_pipe(kern_fd_t fds[2])
{
    return (int)kern_syscall(SYS_PIPE, (long)fds, 0, 0, 0);
}

kern_pid_t sys_spawn(const char *name, void (*entry)(void *arg),
                     void *arg, size_t stack_min)
{
    return (kern_pid_t)kern_syscall(SYS_SPAWN, (long)name, (long)entry,
                                     (long)arg, (long)stack_min);
}
