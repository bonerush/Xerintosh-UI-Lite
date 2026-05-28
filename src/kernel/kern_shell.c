/**
 * @file   kern_shell.c
 * @brief  Xeros 内核 Shell 实现
 * @details 通过 /dev/ttyS0 提供交互式命令行界面。
 *          使用命令表化分派（shell_cmd_t + 精确匹配），
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
#define HISTORY_SIZE      16       /* 历史条数（与 kern_shell_cmds.c 一致） */

/* ═══ 外部声明 ═══ */

extern void shell_history_add(const char *cmd);
extern const char *shell_history_get(int index);
extern int shell_history_count(void);

/* ═══ 输出辅助 ═══ */

static void sh_print(kern_fd_t tty, const char *msg)
{
    if (tty >= 0 && msg != NULL) {
        kern_write(tty, msg, strlen(msg));
    }
}

static void sh_print_prompt(kern_fd_t tty, const char *cwd)
{
    char prompt[KERN_PATH_MAX + 8];
    snprintf(prompt, sizeof(prompt), "[%s]$ ", cwd);
    sh_print(tty, prompt);
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
    if (len >= 3 && seq[0] == '\x1b' && seq[1] == '[') {
        if (seq[2] == 'A' || seq[2] == 'B') return seq[2];
    }
    return 0;
}

/* ═══ Shell 任务入口 ═══ */

static void shell_task_main(void *arg)
{
    (void)arg;

    kern_fd_t tty = kern_open("/dev/ttyS0", KERN_O_RDWR);
    if (tty < 0) return;

    sh_print(tty, "\r\nXeros Shell ready. Type 'help' for commands.\r\n");

    static char line[SHELL_BUF_SIZE];
    static size_t pos = 0;
    static char cwd[KERN_PATH_MAX] = "/";

    /* 历史浏览状态 */
    static int hist_browse = -1;  /* -1 = 不在浏览模式，0..15 = 浏览索引 */

    sh_print_prompt(tty, cwd);

    for (;;) {
        /* Phase 3: scope tick — 非阻塞周期数据输出 */
        scope_tick(tty);

        char ch;
        ssize_t n = kern_read(tty, &ch, 1);
        if (n <= 0) { kern_yield(); continue; }

        /* ═══ VT100 转义序列处理 ═══ */

        if (ch == '\x1b') {
            /* 读取转义序列其余部分 */
            char seq[4] = { '\x1b', 0, 0, 0 };
            int seq_len = 1;

            for (int i = 1; i < 4; i++) {
                ssize_t n2 = kern_read(tty, &seq[i], 1);
                if (n2 <= 0) break;
                seq_len++;
                /* '[' 后跟大写字母是方向键的标准格式 */
                if (i == 1 && seq[1] != '[') break;
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
                    sh_print(tty, "\b \b");
                }
                /* 写入历史命令 */
                strncpy(line, entry, SHELL_BUF_SIZE - 1);
                line[SHELL_BUF_SIZE - 1] = '\0';
                sh_print(tty, line);
                pos = strlen(line);
            } else if (arrow == 'B') {
                /* ↓ — 下一条历史 */
                if (hist_browse > 0) {
                    hist_browse--;
                    const char *entry = shell_history_get(hist_browse);
                    if (entry != NULL) {
                        for (size_t i = 0; i < pos; i++) sh_print(tty, "\b \b");
                        strncpy(line, entry, SHELL_BUF_SIZE - 1);
                        line[SHELL_BUF_SIZE - 1] = '\0';
                        sh_print(tty, line);
                        pos = strlen(line);
                    }
                } else {
                    /* 回到最新：清空行 */
                    for (size_t i = 0; i < pos; i++) sh_print(tty, "\b \b");
                    line[0] = '\0';
                    pos = 0;
                    hist_browse = -1;
                }
            }
            continue;
        }

        /* ═══ 普通字符处理 ═══ */

        /* 回显 */
        kern_write(tty, &ch, 1);

        if (ch == '\r' || ch == '\n') {
            line[pos] = '\0';
            sh_print(tty, "\r\n");

            /* 添加到命令历史 */
            shell_history_add(line);

            /* tokenize + 执行 */
            char *tokens[SHELL_MAX_TOKENS];
            int token_count = shell_tokenize(line, tokens, SHELL_MAX_TOKENS);
            if (token_count < 0) {
                sh_print(tty, "shell: unclosed quote\r\n");
            } else if (token_count > 0) {
                shell_exec_cmd(tty, token_count, tokens, cwd, sizeof(cwd));
            }

            sh_print_prompt(tty, cwd);
            pos = 0;
            hist_browse = -1;
        } else if (ch == '\b' || ch == 0x7F) {
            if (pos > 0) pos--;
            hist_browse = -1;  /* 编辑后离开历史浏览 */
        } else if (pos < SHELL_BUF_SIZE - 1) {
            line[pos++] = ch;
            hist_browse = -1;
        }
    }

    kern_close(tty);
}

/* ═══ 公开接口 ═══ */

void kern_shell_init(void)
{
    kern_spawn("shell", shell_task_main, NULL, 4096);
}
