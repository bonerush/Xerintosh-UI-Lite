/**
 * @file   kern_shell.c
 * @brief  Xeros 内核 Shell 实现
 * @details 通过 /dev/ttyS0 提供交互式命令行界面。
 *          支持命令：ps, cat, echo, help
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_shell.h"
#include "kern_vfs.h"
#include "kern_task.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ 常量 ═══ */

#define SHELL_BUF_SIZE    128      /* 输入行缓冲区 */
#define SHELL_PROMPT      "$ "     /* 命令提示符 */

/* ═══ 前向声明 ═══ */

static void shell_print(kern_fd_t tty, const char *msg);
static void shell_println(kern_fd_t tty, const char *msg);
static void cmd_ps(kern_fd_t tty);
static void cmd_cat(kern_fd_t tty, const char *path);
static void cmd_echo(kern_fd_t tty, const char *args);
static void cmd_help(kern_fd_t tty);

/* ═══ 输出辅助 ═══ */

static void shell_print(kern_fd_t tty, const char *msg)
{
    if (tty >= 0 && msg != NULL) {
        kern_write(tty, msg, strlen(msg));
    }
}

static void shell_println(kern_fd_t tty, const char *msg)
{
    shell_print(tty, msg);
    shell_print(tty, "\r\n");
}

/* ═══ 命令实现 ═══ */

static void cmd_ps(kern_fd_t tty)
{
    kern_task_t *task = kern_task_list_head();
    char line[80];

    shell_println(tty, "PID  STATE   NAME");

    while (task != NULL) {
        const char *state_str;
        switch (task->state) {
        case KERN_TASK_READY:    state_str = "READY  "; break;
        case KERN_TASK_RUNNING:  state_str = "RUNNING"; break;
        case KERN_TASK_SLEEPING: state_str = "SLEEP  "; break;
        case KERN_TASK_BLOCKED:  state_str = "BLOCKED"; break;
        case KERN_TASK_ZOMBIE:   state_str = "ZOMBIE "; break;
        default:                 state_str = "?????  "; break;
        }

        snprintf(line, sizeof(line), "%-4d %s %s",
                 (int)task->pid, state_str, task->name);
        shell_println(tty, line);
        task = task->next;
    }
}

static void cmd_cat(kern_fd_t tty, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        shell_println(tty, "Usage: cat <path>");
        return;
    }

    kern_fd_t fd = kern_open(path, KERN_O_RDONLY);
    if (fd < 0) {
        char err[64];
        snprintf(err, sizeof(err), "cat: cannot open '%s'", path);
        shell_println(tty, err);
        return;
    }

    char buf[128];
    ssize_t n;
    while ((n = kern_read(fd, buf, sizeof(buf))) > 0) {
        kern_write(tty, buf, (size_t)n);
    }
    kern_close(fd);
}

static void cmd_echo(kern_fd_t tty, const char *args)
{
    if (args == NULL || args[0] == '\0') {
        shell_println(tty, "");
        return;
    }

    /* 解析 "echo <text> > <path>" */
    const char *redirect = strstr(args, " > ");
    if (redirect != NULL) {
        /* 提取写入路径 */
        const char *path = redirect + 3;
        while (*path == ' ') path++;

        /* 打开文件写入 */
        kern_fd_t fd = kern_open(path, KERN_O_WRONLY);
        if (fd < 0) {
            shell_println(tty, "echo: cannot write to file");
            return;
        }

        /* 写入重定向前的文本 */
        size_t text_len = (size_t)(redirect - args);
        kern_write(fd, args, text_len);
        kern_close(fd);
    } else {
        /* 无重定向，回显到终端 */
        shell_println(tty, args);
    }
}

static void cmd_help(kern_fd_t tty)
{
    shell_println(tty, "Xeros Shell");
    shell_println(tty, "  ps              - list tasks");
    shell_println(tty, "  cat <path>      - read file");
    shell_println(tty, "  echo <text>     - print text");
    shell_println(tty, "  echo <text> > <path> - write to file");
    shell_println(tty, "  help            - this help");
}

/* ═══ 命令分发 ═══ */

static void shell_dispatch(kern_fd_t tty, const char *line)
{
    /* 跳过前导空白 */
    while (*line == ' ' || *line == '\r' || *line == '\n') line++;
    if (*line == '\0') return;

    if (strncmp(line, "ps", 2) == 0 && (line[2] == '\0' || line[2] == ' ' || line[2] == '\r')) {
        cmd_ps(tty);
    } else if (strncmp(line, "cat ", 4) == 0) {
        const char *path = line + 4;
        while (*path == ' ') path++;
        cmd_cat(tty, path);
    } else if (strncmp(line, "echo ", 5) == 0) {
        cmd_echo(tty, line + 5);
    } else if (strncmp(line, "help", 4) == 0) {
        cmd_help(tty);
    } else {
        shell_println(tty, "unknown command. type 'help' for available commands.");
    }
}

/* ═══ Shell 任务入口 ═══ */

#ifndef NATIVE_TEST
#include <Arduino.h>
#endif

static void shell_task_main(void *arg)
{
    (void)arg;

    kern_fd_t tty = kern_open("/dev/ttyS0", KERN_O_RDWR);
    if (tty < 0) {
        return;  /* ttyS0 不可用 */
    }

    shell_println(tty, "Xeros Shell ready. Type 'help' for commands.");
    shell_print(tty, SHELL_PROMPT);

    char line[SHELL_BUF_SIZE];
    size_t pos = 0;

    for (;;) {
        char ch;
        ssize_t n = kern_read(tty, &ch, 1);

        if (n <= 0) {
            /* 没有输入数据，让出 CPU */
#ifdef NATIVE_TEST
            /* 原生测试中，循环处理直到无更多数据 */
            if (n < 0) break;
#endif
            kern_yield();
            continue;
        }

        /* 回显 */
        kern_write(tty, &ch, 1);

        if (ch == '\r' || ch == '\n') {
            line[pos] = '\0';
            shell_print(tty, "\r\n");
            shell_dispatch(tty, line);
            shell_print(tty, SHELL_PROMPT);
            pos = 0;
        } else if (ch == '\b' || ch == 0x7F) {
            if (pos > 0) pos--;
        } else if (pos < SHELL_BUF_SIZE - 1) {
            line[pos++] = ch;
        }
    }

    kern_close(tty);
}

/* ═══ 公开接口 ═══ */

void kern_shell_init(void)
{
    kern_spawn("shell", shell_task_main, NULL, 2048);
}
