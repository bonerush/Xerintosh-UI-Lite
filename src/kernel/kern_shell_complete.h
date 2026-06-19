/**
 * @file   kern_shell_complete.h
 * @brief  Xeros Shell Tab 补全内部接口
 * @details 将补全辅助函数从 kern_shell.c 中抽离，便于单元测试和原子化维护。
 *          这些接口不对外部模块公开，仅在内核 Shell 内部及测试中使用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SHELL_COMPLETE_H
#define KERN_SHELL_COMPLETE_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ Token 定位 ═══ */

/**
 * @brief 获取当前 token 在 line 中的起始位置
 * @param line 输入行缓冲区
 * @param pos  当前光标/输入位置
 * @return 当前 token 第一个字符的索引
 * @note  分隔符与 kern_shell_tokenize 保持一致：空格、tab、\r、\n
 */
size_t shell_token_start(const char *line, size_t pos);

/* ═══ 路径拆分 ═══ */

/**
 * @brief 从 prefix 中拆分出父目录路径和基础名前缀
 * @param cwd      当前工作目录
 * @param prefix   用户已输入的路径前缀（可能是相对或绝对路径）
 * @param dir_out  输出：父目录绝对路径（已规范化，无冗余斜杠）
 * @param dir_size dir_out 缓冲区大小
 * @param base_out 输出：基础名前缀指针（指向 prefix 内部或静态缓冲区）
 * @return true 成功拆分
 * @note  输出的 dir_out 始终为可用于 kern_path_resolve 的绝对路径。
 */
bool shell_parent_path(const char *cwd, const char *prefix,
                       char *dir_out, size_t dir_size,
                       const char **base_out);

/* ═══ 补全入口 ═══ */

/**
 * @brief 对路径参数进行 Tab 补全
 * @param tty        终端 FD
 * @param line       输入行缓冲区
 * @param pos        当前输入位置（传入/传出）
 * @param tok_start  当前 token 起始位置
 * @param cwd        当前工作目录
 * @note  单个匹配时自动补全；目录补全后追加 '/'，普通文件追加 ' '。
 *        多个匹配时先尝试补全最长公共前缀；无公共前缀则列出候选。
 */
void shell_complete_path(kern_fd_t tty, char *line, size_t *pos,
                         size_t tok_start, const char *cwd);

/**
 * @brief 对命令名进行 Tab 补全
 * @param tty       终端 FD
 * @param line      输入行缓冲区
 * @param pos       当前输入位置（传入/传出）
 * @param cwd       当前工作目录（用于重绘提示符）
 */
void shell_complete_command(kern_fd_t tty, char *line, size_t *pos,
                            const char *cwd);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_COMPLETE_H */
