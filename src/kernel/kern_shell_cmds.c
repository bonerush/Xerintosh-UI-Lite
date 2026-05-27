/**
 * @file   kern_shell_cmds.c
 * @brief  Xeros Shell 命令实现
 * @details 包含全部 Shell 命令处理函数和命令表。
 *          文件命令依赖 kern_shell_cmds_internal.h 声明。
 *
 *          命令表化设计：每条命令通过 shell_cmd_t 注册，命令名精确匹配。
 *          支持 kern_shell_register_cmd() 动态注册扩展命令。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_shell_cmds.h"
#include "kern_vfs.h"
#include "kern_task.h"
#include "kern_types.h"
#include "kern_init.h"
#include "kern_version.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "kern_shell_cmds_internal.h"

#ifndef NATIVE_TEST
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#else
#endif

/* ═══ 输出辅助 ═══ */

void sh_print(kern_fd_t tty, const char *msg)
{
    if (tty >= 0 && msg != NULL) {
        kern_write(tty, msg, strlen(msg));
    }
}

void sh_println(kern_fd_t tty, const char *msg)
{
    sh_print(tty, msg);
    sh_print(tty, "\r\n");
}

/* ═══ 路径解析 ═══ */

static const char *resolve_path(const char *cwd, const char *arg,
                               char *out, size_t out_size)
{
    if (arg == NULL || arg[0] == '\0') {
        snprintf(out, out_size, "%s", cwd);
        return out;
    }
    if (arg[0] == '/') {
        snprintf(out, out_size, "%s", arg);
        return out;
    }
    int written = snprintf(out, out_size, "%s/%s", cwd, arg);
    if (written < 0 || (size_t)written >= out_size) return NULL;
    return out;
}

/* ═══ 命令历史（环形缓冲区）═══ */

#define HISTORY_SIZE 16

static char g_history[HISTORY_SIZE][128]; /* 最多 16 条命令，各 128 字节 */
static int  g_hist_count = 0;            /* 历史条数 */
static int  g_hist_index = 0;            /* 写入位置（环形） */

/**
 * @brief 向历史中添加一条命令
 */
void shell_history_add(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') return;
    /* 去重：重复命令不重复记录 */
    int last = (g_hist_count > 0) ? ((g_hist_count - 1) % HISTORY_SIZE) : -1;
    if (last >= 0 && strcmp(g_history[last], cmd) == 0) return;

    strncpy(g_history[g_hist_index], cmd, 127);
    g_history[g_hist_index][127] = '\0';
    g_hist_index = (g_hist_index + 1) % HISTORY_SIZE;
    if (g_hist_count < HISTORY_SIZE) g_hist_count++;
}

/**
 * @brief 获取第 N 条历史（0 = 最新）
 * @return 历史文本，越界或空时返回 NULL
 */
const char *shell_history_get(int index)
{
    if (index < 0 || index >= g_hist_count) return NULL;
    int pos = (g_hist_count < HISTORY_SIZE)
              ? (g_hist_count - 1 - index)
              : ((g_hist_index - 1 - index + HISTORY_SIZE) % HISTORY_SIZE);
    return g_history[pos];
}

/**
 * @brief 获取当前历史条数
 */
int shell_history_count(void)
{
    return g_hist_count;
}

/* ═══ ps — 列出所有任务 ═══ */

static void cmd_ps(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;

    kern_task_t *task = kern_task_list_head();
    char line[100];

    sh_println(tty, "PID  STATE     NAME          STACK");

    while (task != NULL) {
        const char *state_str;
        switch (task->state) {
        case KERN_TASK_READY:    state_str = "READY    "; break;
        case KERN_TASK_RUNNING:  state_str = "RUNNING  "; break;
        case KERN_TASK_SLEEPING: state_str = "SLEEP    "; break;
        case KERN_TASK_BLOCKED:  state_str = "BLOCKED  "; break;
        case KERN_TASK_ZOMBIE:   state_str = "ZOMBIE   "; break;
        default:                 state_str = "?????    "; break;
        }

        snprintf(line, sizeof(line), "%-4d %s %-12s %zu/%zu",
                 (int)task->pid, state_str, task->name,
                 kern_task_stack_usage(task), task->stack_size);
        sh_println(tty, line);
        task = task->next;
    }
}

/* ═══ ls — 列出目录 ═══ */

static void cmd_ls(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    const char *path_arg = (argc > 1) ? argv[1] : NULL;

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, path_arg, abs_path, sizeof(abs_path));
    if (target == NULL) { sh_println(tty, "ls: path too long"); return; }

    kern_dentry_t *dir = kern_path_resolve(target);
    if (dir == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "ls: cannot access '%s': no such entry", target);
        sh_println(tty, err);
        return;
    }

    if (dir->inode != NULL && dir->inode->type != KERN_FILE_DIR) {
        sh_println(tty, target);
        return;
    }

    char header[80];
    snprintf(header, sizeof(header), "%s (%u entries):", target, dir->child_count);
    sh_println(tty, header);

    for (uint8_t i = 0; i < dir->child_count; i++) {
        kern_dentry_t *child = dir->children[i];
        if (child == NULL) continue;

        const char *type_str = "?   ";
        if (child->inode != NULL) {
            switch (child->inode->type) {
            case KERN_FILE_DIR:     type_str = "DIR "; break;
            case KERN_FILE_CHRDEV:  type_str = "CHR "; break;
            case KERN_FILE_REGULAR: type_str = "REG "; break;
            case KERN_FILE_FIFO:    type_str = "FIFO"; break;
            default: break;
            }
        } else {
            type_str = "DIR ";
        }

        char line[80];
        snprintf(line, sizeof(line), "  %s %s", type_str, child->name);
        sh_println(tty, line);
    }
}

/* ═══ cd — 切换工作目录 ═══ */

static void cmd_cd(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)tty;
    const char *path = (argc > 1) ? argv[1] : NULL;

    if (path != NULL && strcmp(path, ".") == 0) return;

    if (path != NULL && strcmp(path, "..") == 0) {
        if (strcmp(cwd, "/") == 0) return;
        char *last = strrchr(cwd, '/');
        if (last == cwd) cwd[1] = '\0';
        else if (last != NULL) *last = '\0';
        return;
    }

    if (path == NULL || path[0] == '\0') {
        strncpy(cwd, "/", cwd_size);
        return;
    }

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, path, abs_path, sizeof(abs_path));
    if (target == NULL) { sh_println(tty, "cd: path too long"); return; }

    kern_dentry_t *dir = kern_path_resolve(target);
    if (dir == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "cd: '%s': no such directory", target);
        sh_println(tty, err);
        return;
    }
    if (dir->inode != NULL && dir->inode->type != KERN_FILE_DIR) {
        char err[64];
        snprintf(err, sizeof(err), "cd: '%s': not a directory", target);
        sh_println(tty, err);
        return;
    }

    strncpy(cwd, target, cwd_size);
    cwd[cwd_size - 1] = '\0';
}

/* ═══ pwd — 打印工作目录 ═══ */

static void cmd_pwd(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd_size;
    sh_println(tty, cwd);
}

/* ═══ cat — 读取文件 ═══ */

static void cmd_cat(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { sh_println(tty, "Usage: cat <path>"); return; }

    kern_fd_t fd = kern_open(argv[1], KERN_O_RDONLY);
    if (fd < 0) {
        char err[64];
        snprintf(err, sizeof(err), "cat: cannot open '%s'", argv[1]);
        sh_println(tty, err);
        return;
    }

    char buf[128];
    ssize_t n;
    while ((n = kern_read(fd, buf, sizeof(buf))) > 0) {
        kern_write(tty, buf, (size_t)n);
    }
    kern_close(fd);
}

/* ═══ cp — 复制文件 ═══ */

static void cmd_cp(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 3) { sh_println(tty, "Usage: cp <src> <dst>"); return; }

    kern_fd_t src = kern_open(argv[1], KERN_O_RDONLY);
    if (src < 0) { sh_println(tty, "cp: cannot open source"); return; }

    kern_fd_t dst = kern_open(argv[2], KERN_O_WRONLY);
    if (dst < 0) { kern_close(src); sh_println(tty, "cp: cannot open destination"); return; }

    char buf[128];
    ssize_t n;
    while ((n = kern_read(src, buf, sizeof(buf))) > 0) {
        kern_write(dst, buf, (size_t)n);
    }
    kern_close(dst);
    kern_close(src);
}

/* ═══ rm — 删除文件/目录 ═══ */

static void cmd_rm(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { sh_println(tty, "Usage: rm <path>"); return; }

    int ret = kern_vfs_unlink(argv[1]);
    if (ret == KERN_ENOENT) {
        sh_println(tty, "rm: no such file or directory");
    } else if (ret == KERN_ENOTEMPTY) {
        sh_println(tty, "rm: directory not empty");
    } else if (ret != KERN_OK) {
        sh_println(tty, "rm: failed");
    }
}

/* ═══ mkdir — 创建目录 ═══ */

static void cmd_mkdir(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { sh_println(tty, "Usage: mkdir <path>"); return; }

    int ret = kern_vfs_mkdir(argv[1]);
    if (ret == KERN_EEXIST) {
        sh_println(tty, "mkdir: directory already exists");
    } else if (ret != KERN_OK) {
        sh_println(tty, "mkdir: failed");
    }
}

/* ═══ touch — 创建空文件 ═══ */

static void cmd_touch(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { sh_println(tty, "Usage: touch <path>"); return; }

    int ret = kern_vfs_touch(argv[1]);
    if (ret == KERN_EEXIST) {
        /* 已存在，静默成功 */
    } else if (ret != KERN_OK) {
        sh_println(tty, "touch: failed");
    }
}

/* ═══ echo — 输出文本 / 重定向写入 ═══ */

static void cmd_echo(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;

    /* 查找 ">" 重定向符号 */
    int redirect_pos = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0) {
            redirect_pos = i;
            break;
        }
    }

    if (redirect_pos > 1 && redirect_pos + 1 < argc) {
        /* 重定向写入 */
        kern_fd_t fd = kern_open(argv[redirect_pos + 1], KERN_O_WRONLY);
        if (fd < 0) {
            sh_println(tty, "echo: cannot write to file");
            return;
        }
        for (int i = 1; i < redirect_pos; i++) {
            if (i > 1) kern_write(fd, " ", 1);
            kern_write(fd, argv[i], strlen(argv[i]));
        }
        kern_close(fd);
    } else {
        /* 回显到终端 */
        for (int i = 1; i < argc; i++) {
            if (i > 1) sh_print(tty, " ");
            sh_print(tty, argv[i]);
        }
        sh_print(tty, "\r\n");
    }
}

/* ═══ reboot — 重启设备 ═══ */

static void cmd_reboot(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    sh_println(tty, "Rebooting...");
    { volatile uint32_t s = 0; while (s < 500000) s++; }
#ifndef NATIVE_TEST
    esp_restart();
#else
    sh_println(tty, "(native: reboot not available)");
#endif
}

/* ═══ help — 帮助 ═══ */

static void cmd_help(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    sh_println(tty, "Xeros Shell — available commands:");

    const shell_cmd_t *cmds = shell_get_builtin_cmds();
    int count = shell_get_builtin_count();
    char line[96];
    for (int i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "  %-12s - %s", cmds[i].name, cmds[i].help);
        sh_println(tty, line);
    }
}

/* ═══ free — 堆内存统计（新增）═══ */

static void cmd_free(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[80];
#ifndef NATIVE_TEST
    snprintf(line, sizeof(line), "free_heap=%" PRIu32 "  min_free=%" PRIu32 "  total=%" PRIu32,
             esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size(),
             heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
#else
    snprintf(line, sizeof(line), "free_heap=N/A (native mode)");
#endif
    sh_println(tty, line);
}

/* ═══ kill — 终止任务（新增）═══ */

static void cmd_kill(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { sh_println(tty, "Usage: kill <pid>"); return; }

    /* 解析 PID */
    long pid_l = 0;
    for (char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '9') { sh_println(tty, "kill: invalid PID"); return; }
        pid_l = pid_l * 10 + (*p - '0');
    }

    kern_task_t *task = kern_task_get((kern_pid_t)pid_l);
    if (task == NULL) {
        sh_println(tty, "kill: no such task");
        return;
    }
    if (task->state == KERN_TASK_ZOMBIE) {
        sh_println(tty, "kill: task already zombie");
        return;
    }

    /* 保护系统关键任务 */
    if (kern_task_is_protected(task)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "kill: cannot kill system task '%s'", task->name);
        sh_println(tty, msg);
        return;
    }

    /* 虚任务：注销而非标记 ZOMBIE（避免 TCB 泄漏） */
    if (task->flags & KERN_TASK_FLAG_VIRTUAL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "task %ld (%s) killed", pid_l, task->name);
        sh_println(tty, msg);
        /* 通知 UI 任务退出当前 user_item */
        extern bool g_xerintosh_exit_requested;
        g_xerintosh_exit_requested = true;
        return;
    }

    task->state = KERN_TASK_ZOMBIE;
    char msg[64];
    snprintf(msg, sizeof(msg), "task %ld (%s) killed", pid_l, task->name);
    sh_println(tty, msg);
}

/* ═══ uname — 系统信息（新增）═══ */

static void cmd_uname(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[128];
    snprintf(line, sizeof(line), "Xeros " XEROS_VERSION_STRING " " XEROS_PLATFORM
             " compiled " __DATE__ " " __TIME__);
    sh_println(tty, line);
}

/* ═══ df — VFS 使用统计（新增）═══ */

/**
 * @brief 递归统计 dentry 和 inode 数量
 */
static void count_dentries(kern_dentry_t *root, int *dentries, int *inodes)
{
    if (root == NULL) return;
    (*dentries)++;
    if (root->inode != NULL) (*inodes)++;

    for (uint8_t i = 0; i < root->child_count; i++) {
        count_dentries(root->children[i], dentries, inodes);
    }
}

static void cmd_df(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;

    kern_dentry_t *root = kern_vfs_get_root();
    int dentries = 0, inodes = 0;
    count_dentries(root, &dentries, &inodes);

    char line[128];
    snprintf(line, sizeof(line), "VFS: %d dentries, %d inodes, %d fd slots",
             dentries, inodes, KERN_MAX_FD_PER_TASK);
    sh_println(tty, line);
}

/* ═══ clear — 清屏（新增）═══ */

static void cmd_clear(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    sh_print(tty, "\x1b[2J\x1b[H");  /* ANSI 清屏 + 光标归位 */
}

/* ═══ history — 命令历史（新增）═══ */

static void cmd_history(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[136];
    for (int i = 0; i < g_hist_count; i++) {
        const char *entry = shell_history_get(i);
        if (entry == NULL) break;
        snprintf(line, sizeof(line), "%4d  %s", i + 1, entry);
        sh_println(tty, line);
    }
}

/* ═══ date — 运行时间（新增）═══ */

#ifndef NATIVE_TEST
#include <Arduino.h>
#endif

static void cmd_date(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
#ifndef NATIVE_TEST
    /* 使用 esp_timer_get_time() 获取微秒级运行时间 */
    int64_t us = esp_timer_get_time();
    uint32_t ms = (uint32_t)(us / 1000);
    uint32_t d = ms / 86400000;
    uint32_t h = (ms % 86400000) / 3600000;
    uint32_t m = (ms % 3600000) / 60000;
    uint32_t s = (ms % 60000) / 1000;
    char line[64];
    snprintf(line, sizeof(line), "Uptime: %" PRIu32 "d %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ".%03" PRIu32,
             d, h, m, s, ms % 1000);
    sh_println(tty, line);
#else
    sh_println(tty, "Uptime: N/A (native mode)");
#endif
}

/* ═══ hexdump — 十六进制查看（新增）═══ */

static void cmd_hexdump(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { sh_println(tty, "Usage: hexdump <path>"); return; }

    kern_fd_t fd = kern_open(argv[1], KERN_O_RDONLY);
    if (fd < 0) {
        sh_println(tty, "hexdump: cannot open file");
        return;
    }

    uint8_t buf[16];
    ssize_t n;
    uint32_t offset = 0;
    char line[80];

    while ((n = kern_read(fd, (char *)buf, sizeof(buf))) > 0) {
        /* 偏移 */
        int pos = snprintf(line, sizeof(line), "%08x  ", offset);

        /* 十六进制部分 */
        for (ssize_t i = 0; i < 16; i++) {
            if (i < n) {
                pos += snprintf(line + pos, sizeof(line) - pos,
                               "%02x ", buf[i]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
            if (i == 7) { line[pos++] = ' '; line[pos] = '\0'; }
        }

        /* ASCII 部分 */
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (ssize_t i = 0; i < (ssize_t)n; i++) {
            char c = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
            if ((size_t)pos < sizeof(line) - 1) line[pos++] = c;
        }
        line[pos] = '\0';

        sh_println(tty, line);
        offset += (uint32_t)n;
    }
    kern_close(fd);
}

/* ═══ 内置命令表 ═══ */

static const shell_cmd_t g_builtin_cmds[] = {
    /* ── 文件系统命令 ── */
    { "ls",      cmd_ls,       "list directory" },
    { "cd",      cmd_cd,       "change directory" },
    { "pwd",     cmd_pwd,      "print working directory" },
    { "cat",     cmd_cat,      "read file" },
    { "cp",      cmd_cp,       "copy file" },
    { "rm",      cmd_rm,       "remove file/dir" },
    { "mkdir",   cmd_mkdir,    "create directory" },
    { "touch",   cmd_touch,    "create empty file" },
    { "echo",    cmd_echo,     "print text (echo text > file to write)" },

    /* ── 系统命令 ── */
    { "ps",      cmd_ps,       "list tasks" },
    { "reboot",  cmd_reboot,   "restart device" },
    { "help",    cmd_help,     "this help" },

    /* ── 新增命令 ── */
    { "free",    cmd_free,     "show heap memory" },
    { "kill",    cmd_kill,     "terminate task <pid>" },
    { "uname",   cmd_uname,    "print system info" },
    { "df",      cmd_df,       "show VFS usage" },
    { "clear",   cmd_clear,    "clear screen" },
    { "history", cmd_history,  "show command history" },
    { "date",    cmd_date,     "show uptime" },
    { "hexdump", cmd_hexdump,  "hex dump <path>" },
};

#define BUILTIN_COUNT (sizeof(g_builtin_cmds) / sizeof(g_builtin_cmds[0]))

/* ═══ 动态命令注册 ═══ */

static shell_cmd_t g_dynamic_cmds[SHELL_MAX_DYNAMIC_CMDS];
static int g_dynamic_count = 0;

int kern_shell_register_cmd(const shell_cmd_t *cmd)
{
    if (cmd == NULL || cmd->name == NULL || cmd->handler == NULL) {
        return KERN_EINVAL;
    }
    if (g_dynamic_count >= SHELL_MAX_DYNAMIC_CMDS) {
        return KERN_ENOSPC;
    }
    g_dynamic_cmds[g_dynamic_count++] = *cmd;
    return KERN_OK;
}

/* ═══ 命令查找 ═══ */

const shell_cmd_t *shell_get_builtin_cmds(void)
{
    return g_builtin_cmds;
}

int shell_get_builtin_count(void)
{
    return (int)BUILTIN_COUNT;
}

const shell_cmd_t *shell_lookup_cmd(const char *name)
{
    if (name == NULL || name[0] == '\0') return NULL;

    /* 先查动态命令 */
    for (int i = 0; i < g_dynamic_count; i++) {
        if (strcmp(g_dynamic_cmds[i].name, name) == 0) {
            return &g_dynamic_cmds[i];
        }
    }

    /* 再查内置命令 */
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(g_builtin_cmds[i].name, name) == 0) {
            return &g_builtin_cmds[i];
        }
    }

    return NULL;
}

void shell_exec_cmd(kern_fd_t tty, int argc, char *argv[],
                   char *cwd, size_t cwd_size)
{
    if (argc <= 0 || argv == NULL || argv[0] == NULL) return;

    const shell_cmd_t *cmd = shell_lookup_cmd(argv[0]);
    if (cmd == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "command not found: %s", argv[0]);
        sh_println(tty, err);
        return;
    }

    cmd->handler(tty, argc, argv, cwd, cwd_size);
}
