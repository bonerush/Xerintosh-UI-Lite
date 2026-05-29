/**
 * @file   kern_shell_cmds.h
 * @brief  Xeros Shell 命令注册表头文件
 * @details 定义 shell_cmd_t 结构体和命令注册接口。
 *          内置命令表 + 动态注册 API。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SHELL_CMDS_H
#define KERN_SHELL_CMDS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 前向声明 ═══ */

typedef int16_t kern_fd_t;

/* ═══ 命令表条目 ═══ */

/**
 * @brief Shell 命令处理函数原型
 * @param tty  输出文件描述符
 * @param argc token 数量（含命令名）
 * @param argv token 数组（argv[0] 为命令名）
 * @param cwd  当前工作目录（可修改）
 * @param cwd_size cwd 缓冲区大小
 */
typedef void (*shell_cmd_handler_t)(kern_fd_t tty, int argc, char *argv[],
                                    char *cwd, size_t cwd_size);

/**
 * @brief Shell 命令条目
 */
typedef struct {
    const char           *name;      /* 命令名（精确匹配） */
    shell_cmd_handler_t   handler;   /* 处理函数 */
    const char           *help;      /* 帮助文本（单行） */
} shell_cmd_t;

/* ═══ 输出辅助 ═══ */

/**
 * @brief 向 shell 终端输出一行文本
 * @param tty 终端文件描述符
 * @param msg 文本（不含换行）
 */
extern void sh_print(kern_fd_t tty, const char *msg);

/**
 * @brief 向 shell 终端输出一行文本 + 换行
 */
extern void sh_println(kern_fd_t tty, const char *msg);

/* ═══ 命令表接口 ═══ */

#define SHELL_MAX_BUILTIN_CMDS  48   /* 最大内置命令数（Phase 3 扩充） */
#define SHELL_MAX_DYNAMIC_CMDS   8   /* 最大动态注册命令数 */

/**
 * @brief  获取内置命令表
 * @return 内置命令表数组指针
 */
extern const shell_cmd_t *shell_get_builtin_cmds(void);

/**
 * @brief  获取内置命令数量
 */
extern int shell_get_builtin_count(void);

/**
 * @brief  注册一条动态命令
 * @param  cmd 命令条目（生命周期由调用者管理）
 * @return KERN_OK 成功，< 0 为错误码
 */
extern int kern_shell_register_cmd(const shell_cmd_t *cmd);

/**
 * @brief  在命令表中查找命令（先动态后内置）
 * @param  name 命令名
 * @return 匹配的 cmd 条目；未找到返回 NULL
 */
extern const shell_cmd_t *shell_lookup_cmd(const char *name);

/**
 * @brief  执行命令（查找 + 调用 handler）
 * @param  tty      输出文件描述符
 * @param  argc     token 数量
 * @param  argv     token 数组
 * @param  cwd      当前工作目录
 * @param  cwd_size cwd 缓冲区大小
 */
extern void shell_exec_cmd(kern_fd_t tty, int argc, char *argv[],
                          char *cwd, size_t cwd_size);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_CMDS_H */
