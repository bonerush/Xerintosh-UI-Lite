/**
 * @file   kern_shell_cmds_internal.h
 * @brief  Shell 命令模块内部共享声明
 * @details Phase 3: 新增 scope 数据结构。
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

/* ═══ Scope 数据结构（Phase 3：实时数据监测）═══ */

#define SCOPE_MAX_VARS 8

typedef struct {
    char   path[KERN_PATH_MAX];
    bool   active;
} scope_var_t;

extern bool     g_scope_running;
extern int      g_scope_period_ms;
extern uint64_t g_scope_last_tick;
extern scope_var_t g_scope_vars[SCOPE_MAX_VARS];
extern int      g_scope_count;

void kern_shell_scope_tick(kern_fd_t tty);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_CMDS_INTERNAL_H */
