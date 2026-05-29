/**
 * @file   kern_ipc.h
 * @brief  Xeros IPC 机制头文件
 * @details 提供匿名 pipe（kern_pipe）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_IPC_H
#define KERN_IPC_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ Pipe 常量 ═══ */

#define KERN_PIPE_BUF_SIZE   256    /* 默认环形缓冲区大小（字节） */
#define KERN_PIPE_MAX         8     /* 最大 pipe 实例数 */

/* ═══ Pipe 接口 ═══ */

/**
 * @brief  创建匿名管道
 * @param  fds 输出数组，fds[0] 为读端，fds[1] 为写端
 * @return KERN_OK 成功，< 0 为错误码
 * @note   pipe 数据流经 256B 环形缓冲区；读端关闭后写端返回 -KERN_EPIPE
 */
extern int kern_pipe(kern_fd_t fds[2]);

#ifdef __cplusplus
}
#endif

#endif /* KERN_IPC_H */
