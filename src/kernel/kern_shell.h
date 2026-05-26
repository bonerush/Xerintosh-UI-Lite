/**
 * @file   kern_shell.h
 * @brief  Xeros 内核 Shell 头文件
 * @details 提供微型交互 Shell，通过 /dev/ttyS0 读写，支持基本命令。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SHELL_H
#define KERN_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 Shell 任务
 * @note  作为内核任务运行，从 /dev/ttyS0 读取命令并执行
 */
extern void kern_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_H */
