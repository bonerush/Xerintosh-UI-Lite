/**
 * @file   kern_shell_parser.h
 * @brief  Xeros Shell 命令行解析器头文件
 * @details 提供 shell_tokenize() 函数，将输入行按空格分割为 token 数组。
 *          支持双引号保护、转义字符。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SHELL_PARSER_H
#define KERN_SHELL_PARSER_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define SHELL_MAX_TOKENS 16   /* 单行命令最大 token 数 */

/* ═══ 解析接口 ═══ */

/**
 * @brief  将输入行分割为 token 数组
 * @param  line       输入行（以 null 结尾）
 * @param  tokens_out 输出 token 指针数组
 * @param  max_tokens tokens_out 的容量
 * @return token 数量（>= 0），-1 表示引号未闭合
 *
 * @note   tokens_out[i] 指向 line 内部的子串（非拷贝），生命周期与 line 相同。
 *         调用处建议拷贝到局部缓冲区，避免原始缓冲区被覆盖。
 *
 *         支持的语法：
 *           - 空格分割 token
 *           - "双引号" 保护含空格的 token
 *           - 转义字符：\n, \t, \\, \", \xNN（两位十六进制）
 */
extern int shell_tokenize(char *line, char *tokens_out[], int max_tokens);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SHELL_PARSER_H */
