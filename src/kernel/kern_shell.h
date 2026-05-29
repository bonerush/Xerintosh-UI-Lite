/**
 * @file   kern_shell.h
 * @brief  Xeros 内核 Shell 头文件
 * @details 提供微型交互 Shell，通过 /dev/ttyS0 读写，支持命令表化分派、
 *          命令历史（↑↓ 浏览）、动态命令注册。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SHELL_H
#define KERN_SHELL_H

#include "kern_shell_cmds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ Shell 生命周期 ═══ */

/**
 * @brief 启动 Shell 任务
 * @note  作为内核任务运行，从 /dev/ttyS0 读取命令并执行。
 *        使用命令表化分派（kern_shell_cmd_t + 精确匹配），
 *        支持 VT100 方向键浏览历史。
 */
extern void kern_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_H */
