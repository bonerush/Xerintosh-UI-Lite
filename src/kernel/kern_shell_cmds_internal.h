/**
 * @file   kern_shell_cmds_internal.h
 * @brief  Shell 命令模块内部共享声明
 * @details 提供 sh_print/sh_println 等输出辅助函数声明，
 *          供 kern_shell_cmds.c 和 kern_shell_cmds_file.c 内部使用。
 *
 * @note   预留：当前文件命令尚未拆分。此头文件供未来拆分时使用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SHELL_CMDS_INTERNAL_H
#define KERN_SHELL_CMDS_INTERNAL_H

#include "kern_types.h"
#include "kern_shell_cmds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 输出辅助（内部共享）═══ */

void sh_print(kern_fd_t tty, const char *msg);
void sh_println(kern_fd_t tty, const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_CMDS_INTERNAL_H */
