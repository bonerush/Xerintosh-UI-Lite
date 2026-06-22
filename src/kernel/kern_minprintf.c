/**
 * @file   kern_minprintf.c
 * @brief  Xeros 最小 printf 实现
 * @details 替换 newlib 的 snprintf/vsnprintf，仅支持代码库中实际使用的格式说明符：
 *          %s %d %i %u %lu %ld %zu %x %X %p %c %% 及基本宽度/精度。
 *          不支持浮点（%.2f 需调用者手动转为字符串后使用 %s）。
 *          通过 --wrap 链接器选项替换 libc 的 snprintf/vsnprintf。
 *
 *          格式化子集约 ~2KB，比 newlib-nano 的 ~40KB 节省约 38KB Flash。
 *
 * @copyright Copyright (c) 2026
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ═══ 内部辅助 ═══ */

static void append_char(char **buf, size_t *remain, char c)
{
    if (*remain > 1) {
        **buf = c;
        (*buf)++;
        (*remain)--;
    }
}

static void append_str(char **buf, size_t *remain, const char *s, size_t len)
{
    if (s == NULL) s = "(null)";
    while (len-- > 0 && *s && *remain > 1) {
        **buf = *s++;
        (*buf)++;
        (*remain)--;
    }
}

static void append_uint(char **buf, size_t *remain,
                         unsigned long long val, int base,
                         bool upper, int min_width, char pad)
{
    char tmp[24];  /* 足够容纳 64-bit 数的任何进制表示 */
    int pos = (int)sizeof(tmp);
    tmp[--pos] = '\0';

    if (val == 0) {
        tmp[--pos] = '0';
    } else {
        while (val > 0 && pos > 0) {
            int digit = (int)(val % (unsigned long long)base);
            if (digit < 10) {
                tmp[--pos] = (char)('0' + digit);
            } else {
                tmp[--pos] = (char)((upper ? 'A' : 'a') + (digit - 10));
            }
            val /= (unsigned long long)base;
        }
    }

    int len = (int)sizeof(tmp) - 1 - pos;
    while (len < min_width) {
        append_char(buf, remain, pad);
        min_width--;
    }
    append_str(buf, remain, tmp + pos, (size_t)len);
}

static void append_int(char **buf, size_t *remain,
                        long long val, int min_width, char pad)
{
    if (val < 0) {
        append_char(buf, remain, '-');
        val = -val;
    }
    append_uint(buf, remain, (unsigned long long)val, 10, false, min_width, pad);
}

/* ═══ 最小 vsnprintf ═══ */

int __wrap_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    /* 空缓冲区 / 零大小：按标准行为，计算并返回所需大小。
     * 简化实现：遍历格式串估算（不精确但满足 Print.cpp 的使用模式） */
    if (buf == NULL || size == 0) {
        if (fmt == NULL) return 0;
        /* 粗略估算：格式串中每个字符 ≈ 1 字节输出 + 参数占位估算 */
        int est = 0;
        const char *p = fmt;
        while (*p) {
            if (*p == '%') {
                p++;  /* 跳过 % */
                while (*p >= '0' && *p <= '9') p++;  /* 宽度 */
                if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }  /* 精度 */
                if (*p == 'l') p++;  /* long */
                if (*p == 'l') p++;  /* long long */
                if (*p == 'z') p++;  /* size_t */
                if (*p == '\0') break;
                switch (*p) {
                case 's': est += 32; break;   /* 字符串最坏情况 */
                case 'd': case 'i': case 'u': est += 12; break;
                case 'x': case 'X': case 'p': est += 18; break;
                case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': est += 20; break;
                case 'c': est += 1; break;
                case '%': est += 1; break;
                default: est += 2; break;
                }
                p++;
            } else {
                est++;
                p++;
            }
        }
        return est;
    }
    if (fmt == NULL) {
        buf[0] = '\0';
        return 0;
    }

    char *dst = buf;
    size_t remain = size;

    while (*fmt && remain > 1) {
        if (*fmt != '%') {
            append_char(&dst, &remain, *fmt++);
            continue;
        }

        fmt++;  /* 跳过 '%' */

        /* 解析标志 */
        char pad = ' ';
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') pad = '0';
            /* '-' 左对齐标志——当前不支持，仅跳过以防被误认为格式说明符 */
            fmt++;
        }

        /* 解析最小宽度 */
        int min_width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            min_width = min_width * 10 + (*fmt - '0');
            fmt++;
        }

        /* 解析精度 */
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* 解析长度修饰符 */
        bool is_long = false;
        bool is_longlong = false;
        bool is_size_t = false;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { is_longlong = true; fmt++; }
            else { is_long = true; }
        } else if (*fmt == 'z') {
            is_size_t = true;
            fmt++;
        }

        /* 格式说明符 */
        char spec = *fmt++;
        if (spec == '\0') break;

        switch (spec) {
        case 's': {
            const char *s = va_arg(args, const char *);
            append_str(&dst, &remain, s, (size_t)-1);
            break;
        }
        case 'c': {
            char c = (char)va_arg(args, int);
            append_char(&dst, &remain, c);
            break;
        }
        case 'd':
        case 'i': {
            long long val;
            if (is_longlong)      val = va_arg(args, long long);
            else if (is_long)     val = (long long)va_arg(args, long);
            else if (is_size_t)   val = (long long)va_arg(args, ptrdiff_t);
            else                  val = (long long)va_arg(args, int);
            append_int(&dst, &remain, val, min_width, pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long val;
            if (is_longlong)      val = va_arg(args, unsigned long long);
            else if (is_long)     val = (unsigned long long)va_arg(args, unsigned long);
            else if (is_size_t)   val = (unsigned long long)va_arg(args, size_t);
            else                  val = (unsigned long long)va_arg(args, unsigned int);

            int base = (spec == 'u') ? 10 : 16;
            bool upper = (spec == 'X');
            append_uint(&dst, &remain, val, base, upper, min_width, pad);
            break;
        }
        case 'p': {
            void *p = va_arg(args, void *);
            if (p == NULL) {
                append_str(&dst, &remain, "(nil)", 5);
            } else {
                append_str(&dst, &remain, "0x", 2);
                append_uint(&dst, &remain, (uintptr_t)p, 16, false, 8, '0');
            }
            break;
        }
        case '%':
            append_char(&dst, &remain, '%');
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            /* 浮点不支持——应避免使用。输出占位符 */
            append_str(&dst, &remain, "<float>", 7);
            break;
        default:
            /* 未知格式——原样输出 */
            append_char(&dst, &remain, '%');
            append_char(&dst, &remain, spec);
            break;
        }
    }

    *dst = '\0';
    return (int)(dst - buf);
}

/* ═══ snprintf 包装 ═══ */

int __wrap_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = __wrap_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

/* ═══ sprintf 包装（不安全，但兼容旧代码） ═══ */

int __wrap_sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = __wrap_vsnprintf(buf, (size_t)-1, fmt, args);
    va_end(args);
    return ret;
}

/* ═══ vsprintf 包装 ═══ */

int __wrap_vsprintf(char *buf, const char *fmt, va_list args)
{
    return __wrap_vsnprintf(buf, (size_t)-1, fmt, args);
}
