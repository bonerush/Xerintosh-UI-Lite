/**
 * @file   kern_shell.c
 * @brief  Xeros 内核 Shell 实现
 * @details 通过 /dev/ttyS0 提供交互式命令行界面。
 *          使用命令表化分派（kern_shell_cmd_t + 精确匹配），
 *          支持 tokenize 解析（引号/转义）、命令历史（↑↓ 浏览）、
 *          内置命令 + 动态扩展。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_shell.h"
#include "kern_vfs.h"
#include "kern_task.h"
#include "kern_types.h"
#include "kern_shell_parser.h"
#include "kern_shell_cmds.h"
#include "kern_shell_cmds_internal.h"
#include "kern_shell_complete.h"

#ifndef NATIVE_TEST
#include "debug_serial.h"
#endif

#include <string.h>
#include <stdio.h>

/* ═══ 常量 ═══ */

#define SHELL_BUF_SIZE    128      /* 输入行缓冲区 */
/* ═══ 外部声明 ═══ */

extern void shell_history_add(const char *cmd);
extern const char *shell_history_get(int index);

/* ═══ 输出辅助 ═══ */

static void kern_shell_print_prompt(kern_fd_t tty, const char *cwd)
{
    char prompt[KERN_PATH_MAX + 8];
    snprintf(prompt, sizeof(prompt), "[%s]$ ", cwd);
    kern_shell_print(tty, prompt);  /* kern_shell_print 由 kern_shell_cmds.c 提供 */
}

/**
 * @brief 清空当前输入行并重新绘制提示符 + 当前输入内容
 * @note  使用 ANSI \x1b[J 清除从光标到屏幕末尾的内容，避免退格跨行时
 *        擦除命令提示符。适用于退格、历史浏览等需要重绘行的场景。
 */
static void shell_redraw_line(kern_fd_t tty, const char *cwd,
                              const char *line, size_t pos)
{
    kern_shell_print(tty, "\r\x1b[J");
    kern_shell_print_prompt(tty, cwd);
    if (pos > 0) {
        kern_write(tty, line, pos);
    }
}

/* ═══ VT100 转义序列解析 ═══ */

/**
 * @brief 解析 VT100 方向键转义序列
 * @param  seq 转义序列缓冲区（以 '\x1b' 开头）
 * @param  len 序列长度
 * @return 'A'=上箭头, 'B'=下箭头, 0=未识别
 * @note   VT100: ESC [ A = 上, ESC [ B = 下
 */
static char vt100_parse_arrow(const char *seq, int len)
{
    if (len >= 3 && seq[0] == '\x1b' && (seq[1] == '[' || seq[1] == 'O')) {
        if (seq[2] == 'A' || seq[2] == 'B') return seq[2];
    }
    return 0;
}

/* ═══ 补全辅助函数 ═══ */

/**
 * @brief 判断字符是否为 shell token 分隔符
 * @note  与 kern_shell_tokenize 保持一致：空格、tab、\r、\n
 */
static bool shell_is_token_delim(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/**
 * @brief 获取当前 token 在 line 中的起始位置
 * @param line 输入行缓冲区
 * @param pos  当前光标/输入位置
 * @return 当前 token 第一个字符的索引
 */
size_t shell_token_start(const char *line, size_t pos)
{
    while (pos > 0 && !shell_is_token_delim(line[pos - 1])) {
        pos--;
    }
    return pos;
}

/**
 * @brief 将 cwd 与相对子路径拼接为绝对路径
 * @param cwd      当前工作目录（必须以 '/' 开头）
 * @param rel_dir  相对目录部分（不含前导 '/'，可能为空）
 * @param dir_out  输出缓冲区
 * @param dir_size 输出缓冲区大小
 * @return true 成功
 * @note  当 cwd 为 "/" 时避免生成 "//xxx"。
 */
static bool shell_join_cwd(const char *cwd, const char *rel_dir,
                           char *dir_out, size_t dir_size)
{
    bool cwd_is_root = (cwd[0] == '/' && cwd[1] == '\0');
    const char *sep = cwd_is_root ? "" : "/";

    int n;
    if (rel_dir == NULL || rel_dir[0] == '\0') {
        n = snprintf(dir_out, dir_size, "%s", cwd);
    } else {
        n = snprintf(dir_out, dir_size, "%s%s%s", cwd, sep, rel_dir);
    }

    return (n >= 0 && (size_t)n < dir_size);
}

bool shell_parent_path(const char *cwd, const char *prefix,
                       char *dir_out, size_t dir_size,
                       const char **base_out)
{
    if (cwd == NULL || cwd[0] != '/') {
        return false;
    }
    if (prefix == NULL || prefix[0] == '\0') {
        if (!shell_join_cwd(cwd, "", dir_out, dir_size)) {
            return false;
        }
        *base_out = "";
        return true;
    }

    const char *last_slash = strrchr(prefix, '/');
    if (last_slash == NULL) {
        /* 纯相对文件名：基于 cwd */
        if (!shell_join_cwd(cwd, "", dir_out, dir_size)) {
            return false;
        }
        if (strlen(prefix) > KERN_NAME_MAX) {
            return false;
        }
        *base_out = prefix;
        return true;
    }

    /* prefix 中包含 '/'：拆出目录部分 */
    size_t dir_len = (size_t)(last_slash - prefix);

    if (prefix[0] == '/') {
        /* 绝对路径：目录部分就是 prefix[0..dir_len) */
        if (dir_len == 0) {
            /* prefix 以 '/' 开头且 slash 在首位 -> 根目录 */
            if (!shell_join_cwd("/", "", dir_out, dir_size)) {
                return false;
            }
        } else {
            if (dir_len >= dir_size) return false;
            memcpy(dir_out, prefix, dir_len);
            dir_out[dir_len] = '\0';
        }
    } else {
        /* 相对路径：cwd + prefix 中的目录部分 */
        char rel_dir[KERN_PATH_MAX];
        if (dir_len >= sizeof(rel_dir)) return false;
        memcpy(rel_dir, prefix, dir_len);
        rel_dir[dir_len] = '\0';
        if (!shell_join_cwd(cwd, rel_dir, dir_out, dir_size)) {
            return false;
        }
    }

    *base_out = last_slash + 1;

    /* 基础名前缀过长：超过 VFS 允许的文件名最大长度 */
    if (strlen(*base_out) > KERN_NAME_MAX) {
        return false;
    }

    return true;
}

/**
 * @brief 判断 dentry 是否为目录
 * @note  VFS 自动创建的中间目录可能没有 inode，但只要有子节点就视为目录。
 */
static bool shell_is_dir(const kern_dentry_t *d)
{
    if (d == NULL) return false;
    if (d->inode != NULL && d->inode->type == KERN_FILE_DIR) return true;
    return d->child_count > 0;
}

/**
 * @brief 规范化路径中的 . 和 .. 分量
 * @param path  待规范化的绝对路径（原地修改）
 * @param size  缓冲区大小
 * @return true 成功
 * @note  仅处理连续的 /、. 和 ..；VFS 本身不支持 . 与 ..，shell 补全前先归一化。
 */
static bool shell_normalize_path(char *path, size_t size)
{
    if (path == NULL || path[0] != '/') return false;

    char *out = path;
    char *p = path;
    *out++ = '/';
    p++;

    while (*p != '\0') {
        while (*p == '/') p++;
        if (*p == '\0') break;

        const char *start = p;
        while (*p != '/' && *p != '\0') p++;
        size_t len = (size_t)(p - start);

        if (len == 1 && start[0] == '.') {
            /* 跳过 . 分量 */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            /* 回退一级：移除末尾的 / 和上一级分量 */
            if (out > path + 1) {
                out--;
                while (out > path && *(out - 1) != '/') out--;
            }
        } else {
            if ((size_t)(out - path) + len + 1 >= size) return false;
            memcpy(out, start, len);
            out += len;
            if (*p == '/') {
                *out++ = '/';
            }
        }
    }

    if (out == path) {
        *out++ = '/';
    }
    *out = '\0';
    return true;
}

/**
 * @brief 对路径参数进行 Tab 补全
 * @param tty        终端 FD
 * @param line       输入行缓冲区
 * @param pos        当前输入位置（传入/传出）
 * @param tok_start  当前 token 起始位置
 * @param cwd        当前工作目录
 * @note 单个匹配时自动补全；目录补全后追加 '/'，普通文件追加 ' '
 */
void shell_complete_path(kern_fd_t tty, char *line, size_t *pos,
                         size_t tok_start, const char *cwd, bool dir_only)
{
    /* 防御性 null 终止：确保 prefix 的 strlen 正确，
     * 避免退格/VT100 序列消耗后 pos 与脏数据之间的 gap 被读入 */
    line[*pos] = '\0';

    const char *prefix = line + tok_start;

    char dir_path[KERN_PATH_MAX];
    const char *base = "";
    if (!shell_parent_path(cwd, prefix, dir_path, sizeof(dir_path), &base)) {
        return;
    }

    kern_dentry_t *dir = kern_path_resolve(dir_path);
    if (dir == NULL) {
        /* VFS 不支持 . 与 ..，尝试归一化后再解析 */
        if (!shell_normalize_path(dir_path, sizeof(dir_path))) {
            return;
        }
        dir = kern_path_resolve(dir_path);
        if (dir == NULL) {
            return;
        }
    }

    size_t base_len = strlen(base);
    const char *matches[16];
    int match_count = 0;

    for (uint16_t i = 0; i < dir->child_count && match_count < 16; i++) {
        kern_dentry_t *child = dir->children[i];
        if (child == NULL) continue;
        if (dir_only && !shell_is_dir(child)) continue;
        if (strncmp(child->name, base, base_len) == 0) {
            matches[match_count++] = child->name;
        }
    }

    if (match_count == 0) {
        return;
    }

    if (match_count == 1) {
        /* dir_only 且用户未输入前缀时，即使只有一个目录也不直接补全，
         * 而是列出候选，避免 cd 命令在未输入前缀时意外进入子目录。 */
        if (dir_only && base_len == 0) {
            kern_shell_print(tty, "\r\n");
            kern_shell_print(tty, "  ");
            kern_shell_print(tty, matches[0]);
            kern_shell_print(tty, "\r\n");
            kern_shell_print_prompt(tty, cwd);
            kern_shell_print(tty, line);
            return;
        }

        /* 单个匹配：补全 */
        const char *match = matches[0];
        const char *suffix = match + base_len;

        /* 找到匹配的 dentry 以判断类型 */
        kern_dentry_t *matched_d = NULL;
        for (uint16_t i = 0; i < dir->child_count; i++) {
            if (dir->children[i] != NULL
                && strcmp(dir->children[i]->name, match) == 0) {
                matched_d = dir->children[i];
                break;
            }
        }
        bool is_dir = shell_is_dir(matched_d);

        size_t suffix_len = strlen(suffix);
        size_t space_left = SHELL_BUF_SIZE - 1 - *pos;
        if (suffix_len > space_left) {
            /* 空间不足以放下补全后缀：静默放弃 */
            return;
        }

        /* 复制补全后缀 */
        memcpy(line + *pos, suffix, suffix_len);
        *pos += suffix_len;
        kern_shell_print(tty, suffix);

        /* 目录且空间足够时追加 '/'；普通文件追加空格；
         * dir_only 模式下匹配已过滤为目录，直接追加 '/' */
        if ((is_dir || dir_only) && *pos < SHELL_BUF_SIZE - 1) {
            line[*pos] = '/';
            (*pos)++;
            kern_shell_print(tty, "/");
        } else if (!is_dir && !dir_only && *pos < SHELL_BUF_SIZE - 1) {
            line[*pos] = ' ';
            (*pos)++;
            kern_shell_print(tty, " ");
        }
        line[*pos] = '\0';
        return;
    }

    /* 多个匹配：先计算最长公共前缀 */
    size_t common_len = strlen(matches[0]);
    for (int i = 1; i < match_count; i++) {
        size_t j = 0;
        while (j < common_len && matches[i][j] != '\0'
               && matches[0][j] == matches[i][j]) {
            j++;
        }
        common_len = j;
    }

    /* 公共前缀长于用户已输入部分：先补全公共前缀 */
    if (common_len > base_len) {
        const char *common_suffix = matches[0] + base_len;
        size_t suffix_len = common_len - base_len;
        if (*pos + suffix_len < SHELL_BUF_SIZE - 1) {
            memcpy(line + *pos, common_suffix, suffix_len);
            *pos += suffix_len;
            line[*pos] = '\0';
            kern_shell_print(tty, common_suffix);
        }
        return;
    }

    /* 无公共前缀可补：列出候选 */
    kern_shell_print(tty, "\r\n");
    for (int i = 0; i < match_count; i++) {
        kern_shell_print(tty, "  ");
        kern_shell_print(tty, matches[i]);
        if (i + 1 < match_count) {
            kern_shell_print(tty, "\r\n");
        }
    }
    kern_shell_print(tty, "\r\n");
    kern_shell_print_prompt(tty, cwd);
    kern_shell_print(tty, line);
}


void shell_complete_command(kern_fd_t tty, char *line, size_t *pos,
                            const char *cwd)
{
    if (*pos == 0) return;

    line[*pos] = '\0';
    const kern_shell_cmd_t *cmds = kern_shell_get_builtin_cmds();
    int count = kern_shell_get_builtin_count();

    const char *matches[SHELL_MAX_BUILTIN_CMDS];
    int match_count = 0;
    for (int i = 0; i < count && match_count < SHELL_MAX_BUILTIN_CMDS; i++) {
        if (strncmp(cmds[i].name, line, *pos) == 0) {
            matches[match_count++] = cmds[i].name;
        }
    }

    if (match_count == 1) {
        const char *suffix = matches[0] + *pos;
        kern_shell_print(tty, suffix);
        kern_shell_print(tty, " ");

        size_t suffix_len = strlen(suffix);
        size_t copy_len = suffix_len;
        if (*pos + copy_len >= SHELL_BUF_SIZE - 1) {
            copy_len = SHELL_BUF_SIZE - 2 - *pos;
        }
        memcpy(line + *pos, suffix, copy_len);
        *pos += copy_len;
        if (*pos < SHELL_BUF_SIZE - 1) {
            line[*pos] = ' ';
            (*pos)++;
            line[*pos] = '\0';
        }
    } else if (match_count > 1) {
        /* 先计算最长公共前缀 */
        size_t common_len = strlen(matches[0]);
        for (int i = 1; i < match_count; i++) {
            size_t j = 0;
            while (j < common_len && matches[i][j] != '\0'
                   && matches[0][j] == matches[i][j]) {
                j++;
            }
            common_len = j;
        }

        /* 公共前缀长于已输入：补全公共前缀 */
        if (common_len > *pos) {
            const char *common_suffix = matches[0] + *pos;
            size_t suffix_len = common_len - *pos;
            if (*pos + suffix_len < SHELL_BUF_SIZE - 1) {
                memcpy(line + *pos, common_suffix, suffix_len);
                *pos += suffix_len;
                line[*pos] = '\0';
                kern_shell_print(tty, common_suffix);
            }
            return;
        }

        /* 无更多公共前缀：列出候选 */
        kern_shell_print(tty, "\r\n");
        for (int i = 0; i < match_count; i++) {
            kern_shell_print(tty, "  ");
            kern_shell_print(tty, matches[i]);
            kern_shell_print(tty, "\r\n");
        }
        kern_shell_print_prompt(tty, cwd);
        kern_shell_print(tty, line);
    }
}

/* ═══ Shell 任务入口 ═══ */

static void shell_task_main(void *arg)
{
    (void)arg;

    kern_fd_t tty = kern_open("/dev/ttyS0", KERN_O_RDWR);
    if (tty < 0) return;

    kern_shell_print(tty, "\r\nXeros Shell ready. Type 'help' for commands.\r\n");

    static char line[SHELL_BUF_SIZE];
    static size_t pos = 0;
    static char cwd[KERN_PATH_MAX] = "/";

    /* 历史浏览状态 */
    static int hist_browse = -1;  /* -1 = 不在浏览模式，0..15 = 浏览索引 */

    kern_shell_print_prompt(tty, cwd);

    for (;;) {
        /* Phase 3: scope tick — 非阻塞周期数据输出 */
        kern_shell_scope_tick(tty);

        char ch;
        ssize_t n = kern_read(tty, &ch, 1);
        if (n <= 0) { kern_yield(); continue; }

        /* ═══ VT100 转义序列处理 ═══ */

        if (ch == '\x1b') {
            /*
             * VT100/ANSI 转义序列处理
             * 方向键格式：CSI (\x1b[) 或 SS3 (\x1bO) + 大写字母
             * 串口字节到达有延迟 → 用 retry + kern_yield() 等待后续字节
             */
            char seq[4] = { '\x1b', 0, 0, 0 };
            int seq_len = 1;

            for (int i = 1; i < 4; i++) {
                ssize_t n2 = 0;
                for (int retry = 0; retry < 3; retry++) {
                    n2 = kern_read(tty, &seq[i], 1);
                    if (n2 > 0) break;
                    kern_yield();  /* 让 dev_ttyS0_poll() 有机会填补环形缓冲区 */
                }
                if (n2 <= 0) break;
                seq_len++;
                /* CSI (\x1b[) 或 SS3 (\x1bO) 后跟大写字母 = 方向键 */
                if (i == 1 && seq[1] != '[' && seq[1] != 'O') break;
                if (i >= 2 && seq[i] >= 'A' && seq[i] <= 'Z') break;
            }

            char arrow = vt100_parse_arrow(seq, seq_len);
            if (arrow == 'A') {
                /* ↑ — 上一条历史 */
                if (hist_browse < 0) hist_browse = 0;
                else hist_browse++;

                const char *entry = shell_history_get(hist_browse);
                if (entry == NULL) {
                    hist_browse--; /* 到头了，不动 */
                    continue;
                }

                /* 清屏当前行并重新绘制提示符 */
                shell_redraw_line(tty, cwd, line, 0);
                /* 写入历史命令 */
                strncpy(line, entry, SHELL_BUF_SIZE - 1);
                line[SHELL_BUF_SIZE - 1] = '\0';
                kern_shell_print(tty, line);
                pos = strlen(line);
            } else if (arrow == 'B') {
                /* ↓ — 下一条历史 */
                if (hist_browse > 0) {
                    hist_browse--;
                    const char *entry = shell_history_get(hist_browse);
                    if (entry != NULL) {
                        shell_redraw_line(tty, cwd, line, 0);
                        strncpy(line, entry, SHELL_BUF_SIZE - 1);
                        line[SHELL_BUF_SIZE - 1] = '\0';
                        kern_shell_print(tty, line);
                        pos = strlen(line);
                    }
                } else {
                    /* 回到最新：清空行 */
                    shell_redraw_line(tty, cwd, line, 0);
                    line[0] = '\0';
                    pos = 0;
                    hist_browse = -1;
                }
            }
            continue;
        }

        /* ═══ 退格处理（拦截在回显之前，重绘当前行）═══ */
        if (ch == '\b' || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                hist_browse = -1;
                /* 重绘整行：避免 \b 跨物理行时擦除命令提示符 */
                shell_redraw_line(tty, cwd, line, pos);
            } else {
                /* 已到行首：响铃提示 */
                kern_shell_print(tty, "\x07");
            }
            continue;
        }

        /* ═══ Tab 自动补全 ═══
         * 第一个 token 补全命令名；后续 token 补全 VFS 路径。
         * cd 命令仅补全目录（dir_only = true）。 */
        if (ch == '\t') {
            line[pos] = '\0';  /* 防御性 null 终止 */
            size_t tok_start = shell_token_start(line, pos);
            if (tok_start == 0) {
                shell_complete_command(tty, line, &pos, cwd);
            } else {
                bool dir_only = (strncmp(line, "cd ", 3) == 0);
                shell_complete_path(tty, line, &pos, tok_start, cwd, dir_only);
            }
            hist_browse = -1;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            line[pos] = '\0';
            kern_shell_print(tty, "\r\n");

            /* 添加到命令历史 */
            shell_history_add(line);

            /* tokenize + 执行 */
            char *tokens[SHELL_MAX_TOKENS];
            int token_count = kern_shell_tokenize(line, tokens, SHELL_MAX_TOKENS);
            if (token_count < 0) {
                kern_shell_print(tty, "shell: unclosed quote\r\n");
            } else if (token_count > 0) {
                kern_shell_exec_cmd(tty, token_count, tokens, cwd, sizeof(cwd));
            }

            kern_shell_print_prompt(tty, cwd);
            pos = 0;
            hist_browse = -1;
        } else if (pos < SHELL_BUF_SIZE - 1) {
            /* 缓冲区有空间才回显并记录 */
            kern_write(tty, &ch, 1);
            line[pos++] = ch;
            hist_browse = -1;
        }
    }
    /* 理论上不可达：shell 任务无退出路径，tty 由任务清理钩子释放 */
}

/* ═══ 公开接口 ═══ */

void kern_shell_init(void)
{
    kern_spawn("shell", shell_task_main, NULL, 4096);
}
