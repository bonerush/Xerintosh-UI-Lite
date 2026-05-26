/**
 * @file   kern_ipc.h
 * @brief  Xeros IPC 机制头文件
 * @details 提供匿名 pipe（kern_pipe）和命名消息队列（kern_mq_*）。
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

/* ═══ Message Queue 常量 ═══ */

#define KERN_MQ_NAME_MAX      31    /* 队列名称最大长度 */
#define KERN_MQ_MSG_SIZE     128    /* 单条消息最大长度 */
#define KERN_MQ_MAX_QUEUES     8    /* 最大队列数 */
#define KERN_MQ_MAX_MSGS      16    /* 每个队列最大消息数 */

/* ═══ Pipe 接口 ═══ */

/**
 * @brief  创建匿名管道
 * @param  fds 输出数组，fds[0] 为读端，fds[1] 为写端
 * @return KERN_OK 成功，< 0 为错误码
 * @note   pipe 数据流经 256B 环形缓冲区；读端关闭后写端返回 -KERN_EPIPE
 */
extern int kern_pipe(kern_fd_t fds[2]);

/* ═══ Message Queue 接口 ═══ */

/**
 * @brief  打开或创建命名消息队列
 * @param  name 队列名称（用于跨任务查找）
 * @return >= 0 为文件描述符，< 0 为错误码
 * @note   若同名队列已存在则复用，否则创建新队列
 */
extern kern_fd_t kern_mq_open(const char *name);

/**
 * @brief  发送消息到队列
 * @param  fd   队列文件描述符
 * @param  type 消息类型标签（用户自定义，0-255）
 * @param  data 消息内容
 * @param  len  消息长度
 * @return KERN_OK 成功，< 0 为错误码
 */
extern int kern_mq_send(kern_fd_t fd, uint8_t type,
                        const void *data, size_t len);

/**
 * @brief  从队列接收消息（按类型过滤）
 * @param  fd   队列文件描述符
 * @param  type 期望的消息类型（0xFF 匹配所有类型）
 * @param  data 输出缓冲区
 * @param  len  缓冲区长度
 * @return > 0 为实际接收长度，0 为无匹配消息，< 0 为错误码
 */
extern ssize_t kern_mq_recv(kern_fd_t fd, uint8_t type,
                            void *data, size_t len);

/**
 * @brief  关闭消息队列
 * @param  fd 队列文件描述符
 * @return KERN_OK 成功，< 0 为错误码
 */
extern int kern_mq_close(kern_fd_t fd);

#ifdef __cplusplus
}
#endif

#endif /* KERN_IPC_H */
