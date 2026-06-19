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
 * @brief 获取当前 token 在 line 中的起始位置
 * @param line 输入行缓冲区
 * @param pos  当前光标/输入位置
 * @return 当前 token 第一个字符的索引
 */
static size_t shell_token_start(const char *line, size_t pos)
{
    while (pos > 0 && line[pos - 1] != ' ') {
        pos--;
    }
    return pos;
}

/**
 * @brief 从 prefix 中拆分出父目录路径和基础名前缀
 * @param cwd      当前工作目录
 * @param prefix   用户已输入的路径前缀（可能是相对或绝对路径）
 * @param dir_out  输出：父目录绝对路径
 * @param dir_size dir_out 缓冲区大小
 * @param base_out 输出：基础名前缀指针（指向 prefix 内部或静态缓冲区）
 * @return true 成功拆分
 */
static bool shell_parent_path(const char *cwd, const char *prefix,
                              char *dir_out, size_t dir_size,
                              const char **base_out)
{
    if (prefix == NULL || prefix[0] == '\0') {
        snprintf(dir_out, dir_size, "%s", cwd);
        *base_out = "";
        return true;
    }

    if (prefix[0] == '/') {
        /* 绝对路径：拆分出目录部分和基础名 */
        const char *last_slash = strrchr(prefix, '/');
        if (last_slash == prefix) {
            snprintf(dir_out, dir_size, "/");
            *base_out = prefix + 1;
        } else {
            size_t dir_len = (size_t)(last_slash - prefix);
            if (dir_len >= dir_size) return false;
            memcpy(dir_out, prefix, dir_len);
            dir_out[dir_len] = '\0';
            *base_out = last_slash + 1;
        }
    } else {
        /* 相对路径：基于 cwd */
        const char *last_slash = strrchr(prefix, '/');
        if (last_slash == NULL) {
            snprintf(dir_out, dir_size, "%s", cwd);
            *base_out = prefix;
        } else {
            size_t rel_dir_len = (size_t)(last_slash - prefix);
            int n = snprintf(dir_out, dir_size, "%s/", cwd);
            if (n < 0 || (size_t)n >= dir_size) return false;
            size_t used = (size_t)n;
            if (used + rel_dir_len >= dir_size) return false;
            memcpy(dir_out + used, prefix, rel_dir_len);
            dir_out[used + rel_dir_len] = '\0';
            *base_out = last_slash + 1;
        }
    }
    return true;
}

/**
 * @brief 判断 dentry 是否为目录
 */
static bool shell_is_dir(const kern_dentry_t *d)
{
    return d != NULL && d->inode != NULL && d->inode->type == KERN_FILE_DIR;
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
static void shell_complete_path(kern_fd_t tty, char *line, size_t *pos,
                                size_t tok_start, const char *cwd)
{
    const char *prefix = line + tok_start;
    size_t prefix_len = *pos - tok_start;

    char dir_path[KERN_PATH_MAX];
    const char *base = "";
    if (!shell_parent_path(cwd, prefix, dir_path, sizeof(dir_path), &base)) {
        return;
    }

    kern_dentry_t *dir = kern_path_resolve(dir_path);
    if (dir == NULL) {
        return;
    }

    const char *matches[16];
    int match_count = 0;
    size_t base_len = strlen(base);

    for (uint8_t i = 0; i < dir->child_count && match_count < 16; i++) {
        kern_dentry_t *child = dir->children[i];
        if (child == NULL) continue;
        if (strncmp(child->name, base, base_len) == 0) {
            matches[match_count++] = child->name;
        }
    }

    if (match_count == 0) {
        return;
    }

    if (match_count == 1) {
        /* 单个匹配：补全 */
        const char *match = matches[0];
        const char *suffix = match + base_len;
        bool is_dir = false;

        /* 找到匹配的 dentry 以判断类型 */
        kern_dentry_t *matched_d = NULL;
        for (uint8_t i = 0; i < dir->child_count; i++) {
            if (dir->children[i] != NULL && strcmp(dir->children[i]->name, match) == 0) {
                matched_d = dir->children[i];
                break;
            }
        }
        is_dir = shell_is_dir(matched_d);

        size_t suffix_len = strlen(suffix);
        size_t append_len = suffix_len + (is_dir ? 1 : 0);
        if (*pos + append_len >= SHELL_BUF_SIZE - 1) {
            /* 空间不足：静默截断 */
            append_len = SHELL_BUF_SIZE - 1 - *pos;
            is_dir = false;  /* 截断后不再追加分隔符 */
        }

        if (append_len > 0) {
            memcpy(line + *pos, suffix, append_len);
            *pos += append_len;
            kern_shell_print(tty, suffix);
            if (append_len > suffix_len) {
                kern_shell_print(tty, "/");
            }
        }

        /* 目录且成功追加 '/'，或普通文件，都补一个空格 */
        if (*pos < SHELL_BUF_SIZE - 1) {
            line[*pos] = ' ';
            (*pos)++;
            kern_shell_print(tty, " ");
        }
        line[*pos] = '\0';
        return;
    }

    /* 多个匹配：列出候选 */
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

/**
 * @brief 对命令名进行 Tab 补全
 * @param tty       终端 FD
 * @param line      输入行缓冲区
 * @param pos       当前输入位置（传入/传出）
 */
static void shell_complete_command(kern_fd_t tty, char *line, size_t *pos,
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

                /* 清屏当前行 */
                for (size_t i = 0; i < pos; i++) {
                    kern_shell_print(tty, "\b \b");
                }
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
                        for (size_t i = 0; i < pos; i++) kern_shell_print(tty, "\b \b");
                        strncpy(line, entry, SHELL_BUF_SIZE - 1);
                        line[SHELL_BUF_SIZE - 1] = '\0';
                        kern_shell_print(tty, line);
                        pos = strlen(line);
                    }
                } else {
                    /* 回到最新：清空行 */
                    for (size_t i = 0; i < pos; i++) kern_shell_print(tty, "\b \b");
                    line[0] = '\0';
                    pos = 0;
                    hist_browse = -1;
                }
            }
            continue;
        }

        /* ═══ 退格处理（拦截在回显之前，发送擦除序列）═══ */
        if (ch == '\b' || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                /* \b 移到被删字符，空格覆盖，\b 回到空格前。
                 * 行首时 pos==0，不再发送 \b，防止某些终端回退到提示符。 */
                kern_shell_print(tty, "\b \b");
                hist_browse = -1;
            } else {
                /* 已到行首：响铃提示，绝对不要发送 \b */
                kern_shell_print(tty, "\x07");
            }
            continue;
        }

        /* ═══ Tab 自动补全 ═══
         * 第一个 token 补全命令名；后续 token 补全 VFS 路径。 */
        if (ch == '\t') {
            size_t tok_start = shell_token_start(line, pos);
            if (tok_start == 0) {
                shell_complete_command(tty, line, &pos, cwd);
            } else {
                shell_complete_path(tty, line, &pos, tok_start, cwd);
            }
            hist_browse = -1;
            continue;
        }

        /* ═══ 普通字符回显 ═══ */
        kern_write(tty, &ch, 1);

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
