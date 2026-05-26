/**
 * @file   kern_types.h
 * @brief  Xeros 内核基本类型、错误码与常量定义
 * @details 定义内核全局使用的 PID、错误码（Linux errno 子集）、
 *          任务状态、日志级别、文件类型等基础类型。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_TYPES_H
#define KERN_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 错误码（Linux errno 子集） ═══ */

#define KERN_OK         0       /* 成功 */
#define KERN_ERR        (-1)    /* 通用错误 */
#define KERN_ENOENT     (-2)    /* 无此文件或目录 */
#define KERN_EIO        (-5)    /* I/O 错误 */
#define KERN_ENOMEM     (-12)   /* 内存不足 */
#define KERN_EACCES     (-13)   /* 权限不足 */
#define KERN_EEXIST     (-17)   /* 文件已存在 */
#define KERN_ENOTDIR    (-20)   /* 不是目录 */
#define KERN_EISDIR     (-21)   /* 是目录 */
#define KERN_EINVAL     (-22)   /* 参数无效 */
#define KERN_EMFILE     (-24)   /* 打开文件过多 */
#define KERN_ENOSPC     (-28)   /* 空间不足 */
#define KERN_EPIPE      (-32)   /* 管道损坏 */
#define KERN_EBADF      (-9)    /* 无效文件描述符 */
#define KERN_ENOTTY     (-25)   /* 不支持的 ioctl */

/* ═══ 任务相关常量 ═══ */

typedef int16_t kern_pid_t;

#define KERN_PID_INVALID   (-1)   /* 无效 PID */
#define KERN_MAX_TASKS     16     /* 最大并发任务数 */
#define KERN_TASK_NAME_LEN 16     /* 任务名称最大长度 */

/**
 * @brief 任务状态枚举
 */
typedef enum {
    KERN_TASK_READY    = 0,  /* 就绪 */
    KERN_TASK_RUNNING  = 1,  /* 运行中 */
    KERN_TASK_SLEEPING = 2,  /* 睡眠中 */
    KERN_TASK_BLOCKED  = 3,  /* 阻塞中（等待 I/O 等） */
    KERN_TASK_ZOMBIE   = 4,  /* 僵尸（已退出，待回收） */
} kern_task_state_t;

/* ═══ 日志级别 ═══ */

typedef enum {
    KERN_LOG_DEBUG = 0,  /* 调试信息 */
    KERN_LOG_INFO  = 1,  /* 一般信息 */
    KERN_LOG_WARN  = 2,  /* 警告 */
    KERN_LOG_ERROR = 3,  /* 错误 */
    KERN_LOG_PANIC = 4,  /* 致命错误（触发 panic） */
} kern_log_level_t;

/* ═══ 文件类型 ═══ */

typedef enum {
    KERN_FILE_REGULAR = 0,  /* 普通文件 */
    KERN_FILE_DIR     = 1,  /* 目录 */
    KERN_FILE_CHRDEV  = 2,  /* 字符设备 */
    KERN_FILE_BLKDEV  = 3,  /* 块设备 */
    KERN_FILE_FIFO    = 4,  /* 命名管道 */
} kern_file_type_t;

/* ═══ 文件描述符 ═══ */

typedef int16_t kern_fd_t;

#define KERN_FD_INVALID    (-1)
#define KERN_MAX_FD_PER_TASK 8   /* 每个任务最大文件描述符数 */

/* ═══ 栈管理常量 ═══ */

#define KERN_STACK_MIN      1024  /* 初始栈大小（字节） */
#define KERN_STACK_MAX      8192  /* 最大栈大小（字节） */
#define KERN_STACK_GROW     1024  /* 每次增长步进（字节） */
#define KERN_STACK_CANARY   0xDEADC0DE  /* 栈金丝雀值（溢出检测） */

/* ═══ 路径常量 ═══ */

#define KERN_PATH_MAX       64    /* 最大路径长度 */
#define KERN_NAME_MAX       31    /* 文件名最大长度 */

#ifdef __cplusplus
}
#endif

#endif /* KERN_TYPES_H */
