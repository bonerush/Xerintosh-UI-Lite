/**
 * @file   kern_shell_parser.c
 * @brief  Xeros Shell 命令行解析器实现
 * @details 将输入行分割为 token 数组。支持双引号保护、常见转义字符。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_shell_parser.h"
#include <stddef.h>

/* ═══ 转义字符解析 ═══ */

/**
 * @brief  解析单个转义字符
 * @param  src 指向转义字符 '\' 之后的位置
 * @param  dst 输出字符的写入位置
 * @return 消耗的源字符数（不含 '\'），0 表示无法识别
 */
static int shell_parse_escape(const char *src, char *dst)
{
    switch (*src) {
    case 'n':  *dst = '\n'; return 1;
    case 't':  *dst = '\t'; return 1;
    case '\\': *dst = '\\'; return 1;
    case '"':  *dst = '"';  return 1;
    case 'r':  *dst = '\r'; return 1;
    case 'x': {
        /* \xNN — 两位十六进制 */
        char hex_hi = src[1];
        char hex_lo = src[2];
        if (hex_lo == '\0') return 0; /* 不完整 */

        unsigned char val = 0;

        if (hex_hi >= '0' && hex_hi <= '9')      val  = (unsigned char)(hex_hi - '0') << 4;
        else if (hex_hi >= 'a' && hex_hi <= 'f') val  = (unsigned char)(hex_hi - 'a' + 10) << 4;
        else if (hex_hi >= 'A' && hex_hi <= 'F') val  = (unsigned char)(hex_hi - 'A' + 10) << 4;
        else return 0;

        if (hex_lo >= '0' && hex_lo <= '9')      val |= (unsigned char)(hex_lo - '0');
        else if (hex_lo >= 'a' && hex_lo <= 'f') val |= (unsigned char)(hex_lo - 'a' + 10);
        else if (hex_lo >= 'A' && hex_lo <= 'F') val |= (unsigned char)(hex_lo - 'A' + 10);
        else return 0;

        *dst = (char)val;
        return 3; /* \xNN = 4 字符，跳过 '\' 后 3 个 */
    }
    default:
        /* 未识别的转义：保持原样 */
        *dst = *src;
        return 1;
    }
}

/* ═══ Tokenize ═══ */

int shell_tokenize(char *line, char *tokens_out[], int max_tokens)
{
    if (line == NULL || tokens_out == NULL || max_tokens <= 0) {
        return 0;
    }

    int count = 0;
    char *p = line;
    char *dst = line;  /* 原地写入：展开转义后的 token 文本 */

    while (*p != '\0' && count < max_tokens) {
        /* 跳过空白 */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') break;

        /* 记录 token 起始位置 */
        tokens_out[count++] = dst;

        if (*p == '"') {
            /* 双引号保护模式 */
            p++; /* 跳过 '"' */
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    p++; /* 跳过 '\' */
                    int consumed = shell_parse_escape(p, dst);
                    p += consumed;
                    dst++;
                } else {
                    *dst++ = *p++;
                }
            }
            if (*p == '"') {
                p++; /* 跳过闭合 '"' */
            } else {
                /* 引号未闭合 */
                *dst = '\0';
                return -1;
            }
        } else {
            /* 无引号模式：以空白结束 */
            while (*p != '\0' && *p != ' ' && *p != '\t'
                   && *p != '\r' && *p != '\n') {
                if (*p == '\\' && p[1] != '\0') {
                    p++; /* 跳过 '\' */
                    int consumed = shell_parse_escape(p, dst);
                    p += consumed;
                    dst++;
                } else {
                    *dst++ = *p++;
                }
            }
        }

        /*
         * 在写入 '\0' 终止符之前必须先跳过尾随空白，
         * 因为 p 可能指向空白字符，而 dst 即将覆盖它。
         * 若先写入 '\0'，p 读到的将是 '\0'（而非空白），
         * 导致循环提前结束，后续 token 被丢弃。
         */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }

        /* 终止当前 token */
        *dst++ = '\0';
    }

    /* 剩余 token 槽位设为 NULL */
    for (int i = count; i < max_tokens; i++) {
        tokens_out[i] = NULL;
    }

    return count;
}
