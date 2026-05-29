# Shell 解析器（kern_shell_parser）

> **Parent:** [内核子系统总览](index.md) | **Related:** [内核 Shell](kern-shell.md), [Shell 命令实现](kern-shell-cmds.md)

## 概述

`kern_shell_parser` 是 Xeros Shell 的**输入行解析器**，负责将用户输入的原始字符串分割为 token 数组，供命令分发器使用。支持双引号保护、常见转义字符（`\n`、`\t`、`\xNN` 等）和命令行参数分割。

---

## 核心 API

*📄 Source: [kern_shell_parser.c](../../src/kernel/kern_shell_parser.c#L58-L140)*

```c
int kern_shell_tokenize(char *line, char *tokens_out[], int max_tokens);
```

### 参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `line` | `char *` | 输入行（会被原地修改，插入 `\0` 终止符） |
| `tokens_out` | `char *[]` | 输出 token 指针数组 |
| `max_tokens` | `int` | 最大 token 数量，超出时截断 |

### 返回值

- `0`：没有 token（空行或全是空白）
- `N`：成功解析出 N 个 token
- `-1`：引号未闭合

---

### 中文伪代码拆解

```
函数 分词(输入行, 输出数组, 最大数量) {
    if (输入为空 或 输出数组为空 或 最大数量 <= 0) return 0

    跳过前导空白字符
    token计数 = 0
    当前写入位置 = 输入行起始

    while (未到达行尾 且 token计数 < 最大数量) {
        if (当前字符是双引号) {
            // 进入引号模式
            指针前进到引号后
            输出数组[token计数] = 当前位置
            token计数++

            while (未到行尾 且 不是结束引号) {
                if (当前字符是反斜杠) {
                    解析转义字符()
                    将转义结果写入当前写入位置
                } else {
                    复制当前字符到写入位置
                }
                前进
            }

            if (引号未闭合) return -1
            在引号后插入字符串结束符 '\0'
            跳过引号后的空白

        } else {
            // 普通 token
            输出数组[token计数] = 当前位置
            token计数++

            while (未到行尾 且 不是空白) {
                if (当前字符是反斜杠) {
                    解析转义字符()
                } else {
                    复制字符
                }
                前进
            }

            在空白处插入字符串结束符 '\0'
            跳过连续空白
        }
    }

    return token计数
}
```

**核心思想**：原地修改输入字符串（用 `\0` 替换分隔符），输出指针数组指向各个 token 的起始位置。支持引号保护和转义，兼顾效率和功能。

---

## 转义字符解析

*📄 Source: [kern_shell_parser.c](../../src/kernel/kern_shell_parser.c#L20-L54)*

```c
static int shell_parse_escape(const char *src, char *dst)
{
    switch (*src) {
    case 'n':  *dst = '\n'; return 1;
    case 't':  *dst = '\t'; return 1;
    case '\': *dst = '\\'; return 1;
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
```

#### 中文伪代码拆解

```
函数 解析转义(源指针, 目标指针) {
    switch (源指针指向的字符) {
        case 'n':  目标 = 换行符;    return 1
        case 't':  目标 = 制表符;    return 1
        case '\': 目标 = 反斜杠;   return 1
        case '"':  目标 = 双引号;   return 1
        case 'r':  目标 = 回车符;   return 1
        case 'x':
            // 十六进制转义 \xNN
            高位 = src[1]
            低位 = src[2]
            if (低位 == '\0') return 0    // 不完整的转义

            val = 十六进制值(高位) << 4
            val |= 十六进制值(低位)
            目标 = val
            return 3    // 消耗 x + 两位十六进制
        default:
            目标 = 原字符    // 未识别的转义保持原样
            return 1
    }
}
```

---

## 解析示例

| 输入 | 输出 token 数组 | 说明 |
|------|----------------|------|
| `ls /proc` | `["ls", "/proc"]` | 简单分割 |
| `echo "hello world"` | `["echo", "hello world"]` | 引号保护空格 |
| `echo hello\ world` | `["echo", "hello world"]` | 转义空格 |
| `echo "line1\nline2"` | `["echo", "line1\nline2"]` | 转义换行 |
| `echo \x48\x69` | `["echo", "Hi"]` | 十六进制转义 |
| `"unclosed` | `-1` | 引号未闭合错误 |

---

## 设计约束

1. **原地修改**：`kern_shell_tokenize()` 直接修改输入字符串，用 `\0` 替换分隔符。调用者如果需要保留原始字符串，必须事先复制。

2. **缓冲区安全**：所有写入都经过边界检查，`strncpy` + 显式终止符，避免缓冲区溢出。

3. **无动态分配**：解析过程不使用 `malloc`，完全在调用者提供的栈/静态缓冲区上操作，适合嵌入式无堆场景。

---

> **See Also:** [内核 Shell](kern-shell.md) | [Shell 命令实现](kern-shell-cmds.md)
