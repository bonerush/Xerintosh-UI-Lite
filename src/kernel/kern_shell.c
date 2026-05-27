/**
 * @file   kern_shell.c
 * @brief  Xeros 内核 Shell 实现
 * @details 通过 /dev/ttyS0 提供交互式命令行界面。
 *          支持命令：ls, cd, pwd, ps, cat, cp, rm, mkdir, echo, reboot, help
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_shell.h"
#include "kern_vfs.h"
#include "kern_task.h"
#include "kern_types.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NATIVE_TEST
#include <esp_system.h>
#endif

/* ═══ 常量 ═══ */

#define SHELL_BUF_SIZE    128      /* 输入行缓冲区 */

/* ═══ 前向声明 ═══ */

static void shell_print(kern_fd_t tty, const char *msg);
static void shell_println(kern_fd_t tty, const char *msg);
static void shell_print_prompt(kern_fd_t tty, const char *cwd);
static const char *shell_resolve_path(const char *cwd, const char *arg,
                                      char *out, size_t out_size);
static void cmd_ls(kern_fd_t tty, const char *cwd, const char *path);
static void cmd_cd(kern_fd_t tty, char *cwd, size_t cwd_size, const char *path);
static void cmd_pwd(kern_fd_t tty, const char *cwd);
static void cmd_ps(kern_fd_t tty);
static void cmd_cat(kern_fd_t tty, const char *path);
static void cmd_cp(kern_fd_t tty, const char *src, const char *dst);
static void cmd_rm(kern_fd_t tty, const char *path);
static void cmd_mkdir(kern_fd_t tty, const char *path);
static void cmd_touch(kern_fd_t tty, const char *path);
static void cmd_echo(kern_fd_t tty, const char *args);
static void cmd_reboot(kern_fd_t tty);
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

/**
 * @brief 打印带当前目录的提示符（如 [/dev]$ ）
 */
static void shell_print_prompt(kern_fd_t tty, const char *cwd)
{
    char prompt[KERN_PATH_MAX + 8];
    snprintf(prompt, sizeof(prompt), "[%s]$ ", cwd);
    shell_print(tty, prompt);
}

/* ═══ 路径解析辅助 ═══ */

/**
 * @brief 将相对/绝对路径解析为绝对路径
 * @param cwd      当前工作目录（以 "/" 开头，不以 "/" 结尾）
 * @param arg      用户输入的路径参数
 * @param out      输出缓冲区
 * @param out_size 输出缓冲区大小
 * @return 解析后的绝对路径（指向 out）；路径过长时返回 NULL
 */
static const char *shell_resolve_path(const char *cwd, const char *arg,
                                      char *out, size_t out_size)
{
    if (arg == NULL || arg[0] == '\0') {
        /* 无参数 → 返回 CWD */
        snprintf(out, out_size, "%s", cwd);
        return out;
    }

    if (arg[0] == '/') {
        /* 绝对路径：直接使用 */
        snprintf(out, out_size, "%s", arg);
        return out;
    }

    /* 相对路径：拼接 CWD + "/" + arg */
    int written = snprintf(out, out_size, "%s/%s", cwd, arg);
    if (written < 0 || (size_t)written >= out_size) {
        return NULL;
    }
    return out;
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

/* ─── ls —— 列出目录内容 ─── */

static void cmd_ls(kern_fd_t tty, const char *cwd, const char *path)
{
    char abs_path[KERN_PATH_MAX];
    const char *target = shell_resolve_path(cwd, path, abs_path, sizeof(abs_path));
    if (target == NULL) {
        shell_println(tty, "ls: path too long");
        return;
    }

    kern_dentry_t *dir = kern_path_resolve(target);
    if (dir == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "ls: cannot access '%s': no such file or directory", target);
        shell_println(tty, err);
        return;
    }

    /* 目录节点可能没有 inode，有 inode 但非目录也算非法 */
    if (dir->inode != NULL && dir->inode->type != KERN_FILE_DIR) {
        /* 不是目录 —— 只打印文件名，类似 ls 普通文件 */
        shell_println(tty, target);
        return;
    }

    /* 打印表头 */
    char header[64];
    snprintf(header, sizeof(header), "%s (%u entries):", target, dir->child_count);
    shell_println(tty, header);

    for (uint8_t i = 0; i < dir->child_count; i++) {
        kern_dentry_t *child = dir->children[i];
        if (child == NULL) continue;

        const char *type_str;
        if (child->inode != NULL) {
            switch (child->inode->type) {
            case KERN_FILE_DIR:    type_str = "DIR "; break;
            case KERN_FILE_CHRDEV: type_str = "CHR "; break;
            case KERN_FILE_REGULAR: type_str = "REG "; break;
            case KERN_FILE_FIFO:   type_str = "FIFO"; break;
            default:               type_str = "?   "; break;
            }
        } else {
            type_str = "DIR ";  /* 无 inode 视为空目录 */
        }

        char line[80];
        snprintf(line, sizeof(line), "  %s %-24s", type_str, child->name);
        shell_println(tty, line);
    }
}

/* ─── cd —— 切换工作目录 ─── */

static void cmd_cd(kern_fd_t tty, char *cwd, size_t cwd_size, const char *path)
{
    /* cd . —— 留在当前目录 */
    if (path != NULL && strcmp(path, ".") == 0) {
        return;
    }

    /* cd .. —— 返回上级目录 */
    if (path != NULL && strcmp(path, "..") == 0) {
        if (strcmp(cwd, "/") == 0) {
            return;  /* 已在根目录 */
        }
        char *last_slash = strrchr(cwd, '/');
        if (last_slash == cwd) {
            /* 一级子目录 → 回到根 */
            cwd[1] = '\0';
        } else if (last_slash != NULL) {
            *last_slash = '\0';
        }
        return;
    }

    /* cd（无参数）→ 回到根目录 */
    if (path == NULL || path[0] == '\0') {
        strncpy(cwd, "/", cwd_size);
        return;
    }

    char abs_path[KERN_PATH_MAX];
    const char *target = shell_resolve_path(cwd, path, abs_path, sizeof(abs_path));
    if (target == NULL) {
        shell_println(tty, "cd: path too long");
        return;
    }

    kern_dentry_t *dir = kern_path_resolve(target);
    if (dir == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "cd: '%s': no such file or directory", target);
        shell_println(tty, err);
        return;
    }

    /* 检查是否为目录（有 inode 且非 DIR，或无 inode 视为空目录均可） */
    if (dir->inode != NULL && dir->inode->type != KERN_FILE_DIR) {
        char err[64];
        snprintf(err, sizeof(err), "cd: '%s': not a directory", target);
        shell_println(tty, err);
        return;
    }

    strncpy(cwd, target, cwd_size);
    cwd[cwd_size - 1] = '\0';
}

/* ─── pwd —— 打印当前工作目录 ─── */

static void cmd_pwd(kern_fd_t tty, const char *cwd)
{
    shell_println(tty, cwd);
}

/* ─── cp —— 复制文件 ─── */

static void cmd_cp(kern_fd_t tty, const char *src, const char *dst)
{
    if (src == NULL || src[0] == '\0' || dst == NULL || dst[0] == '\0') {
        shell_println(tty, "Usage: cp <src> <dst>");
        return;
    }

    kern_fd_t fd_src = kern_open(src, KERN_O_RDONLY);
    if (fd_src < 0) {
        char err[64];
        snprintf(err, sizeof(err), "cp: cannot open '%s'", src);
        shell_println(tty, err);
        return;
    }

    kern_fd_t fd_dst = kern_open(dst, KERN_O_WRONLY);
    if (fd_dst < 0) {
        char err[64];
        snprintf(err, sizeof(err), "cp: cannot open '%s'", dst);
        shell_println(tty, err);
        kern_close(fd_src);
        return;
    }

    char buf[128];
    ssize_t n;
    while ((n = kern_read(fd_src, buf, sizeof(buf))) > 0) {
        kern_write(fd_dst, buf, (size_t)n);
    }

    kern_close(fd_dst);
    kern_close(fd_src);
}

/* ─── rm —— 删除文件或空目录 ─── */

static void cmd_rm(kern_fd_t tty, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        shell_println(tty, "Usage: rm <path>");
        return;
    }

    int ret = kern_vfs_unlink(path);
    if (ret == KERN_ENOENT) {
        char err[64];
        snprintf(err, sizeof(err), "rm: cannot remove '%s': no such file or directory", path);
        shell_println(tty, err);
    } else if (ret == KERN_ENOTEMPTY) {
        char err[64];
        snprintf(err, sizeof(err), "rm: cannot remove '%s': directory not empty", path);
        shell_println(tty, err);
    } else if (ret != KERN_OK) {
        char err[64];
        snprintf(err, sizeof(err), "rm: cannot remove '%s': error %d", path, ret);
        shell_println(tty, err);
    }
}

/* ─── mkdir —— 创建目录 ─── */

static void cmd_mkdir(kern_fd_t tty, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        shell_println(tty, "Usage: mkdir <path>");
        return;
    }

    int ret = kern_vfs_mkdir(path);
    if (ret == KERN_EEXIST) {
        shell_println(tty, "mkdir: directory already exists");
    } else if (ret != KERN_OK) {
        char err[64];
        snprintf(err, sizeof(err), "mkdir: cannot create '%s': error %d", path, ret);
        shell_println(tty, err);
    }
}

/* ─── touch —— 创建空文件 ─── */

static void cmd_touch(kern_fd_t tty, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        shell_println(tty, "Usage: touch <path>");
        return;
    }

    int ret = kern_vfs_touch(path);
    if (ret == KERN_EEXIST) {
        /* 文件已存在，touch 不报错（更新访问时间在嵌入式中无意义） */
    } else if (ret != KERN_OK) {
        char err[64];
        snprintf(err, sizeof(err), "touch: cannot create '%s': error %d", path, ret);
        shell_println(tty, err);
    }
}

/* ─── reboot —— 重启设备 ─── */

static void cmd_reboot(kern_fd_t tty)
{
    shell_println(tty, "System rebooting...");

    /* 给串口一点时间发送完最后的消息 */
    {
        volatile uint32_t spin = 0;
        while (spin < 500000) spin++;
    }

#ifndef NATIVE_TEST
    esp_restart();
#else
    shell_println(tty, "(native: reboot not available, use Ctrl+C)");
#endif
}

/* ─── help —— 打印帮助信息 ─── */

static void cmd_help(kern_fd_t tty)
{
    shell_println(tty, "Xeros Shell");
    shell_println(tty, "  ls [path]      - list directory");
    shell_println(tty, "  cd [path]      - change directory");
    shell_println(tty, "  pwd             - print working directory");
    shell_println(tty, "  ps              - list tasks");
    shell_println(tty, "  cat <path>      - read file");
    shell_println(tty, "  cp <src> <dst>  - copy file");
    shell_println(tty, "  rm <path>       - remove file/dir");
    shell_println(tty, "  mkdir <path>    - create directory");
    shell_println(tty, "  touch <path>     - create empty file");
    shell_println(tty, "  echo <text>     - print text");
    shell_println(tty, "  echo <text> > <path> - write to file");
    shell_println(tty, "  reboot          - restart device");
    shell_println(tty, "  help            - this help");
}

/* ═══ 命令分发 ═══ */

static void shell_dispatch(kern_fd_t tty, char *cwd, size_t cwd_size,
                           const char *line)
{
    /* 跳过前导空白 */
    while (*line == ' ' || *line == '\r' || *line == '\n') line++;
    if (*line == '\0') return;

    /*
     * 命令匹配顺序很重要：
     * - 无参数命令（ps, pwd, reboot, help）需要精确匹配或后跟空白
     * - 带参数命令用 "cmd " 前缀匹配
     * - ls/cd 可带可不带参数，需要灵活匹配
     */

    /* ls —— 可带可选路径 */
    if (strncmp(line, "ls", 2) == 0 && (line[2] == '\0' || line[2] == ' ' || line[2] == '\r')) {
        const char *path = line + 2;
        while (*path == ' ') path++;
        cmd_ls(tty, cwd, path);
    }
    /* cd —— 可带可选路径 */
    else if (strncmp(line, "cd", 2) == 0 && (line[2] == '\0' || line[2] == ' ' || line[2] == '\r')) {
        const char *path = line + 2;
        while (*path == ' ') path++;
        cmd_cd(tty, cwd, cwd_size, path);
    }
    /* pwd */
    else if (strncmp(line, "pwd", 3) == 0) {
        cmd_pwd(tty, cwd);
    }
    /* ps */
    else if (strncmp(line, "ps", 2) == 0 && (line[2] == '\0' || line[2] == ' ' || line[2] == '\r')) {
        cmd_ps(tty);
    }
    /* cp <src> <dst> */
    else if (strncmp(line, "cp ", 3) == 0) {
        const char *src = line + 3;
        while (*src == ' ') src++;
        const char *dst = strchr(src, ' ');
        if (dst != NULL) {
            char src_buf[KERN_PATH_MAX];
            size_t src_len = (size_t)(dst - src);
            if (src_len >= sizeof(src_buf)) src_len = sizeof(src_buf) - 1;
            memcpy(src_buf, src, src_len);
            src_buf[src_len] = '\0';

            while (*dst == ' ') dst++;
            char dst_buf[KERN_PATH_MAX];
            const char *dst_end = dst;
            while (*dst_end != '\0' && *dst_end != ' ' && *dst_end != '\r') dst_end++;
            size_t dst_len = (size_t)(dst_end - dst);
            if (dst_len >= sizeof(dst_buf)) dst_len = sizeof(dst_buf) - 1;
            memcpy(dst_buf, dst, dst_len);
            dst_buf[dst_len] = '\0';

            cmd_cp(tty, src_buf, dst_buf);
        } else {
            shell_println(tty, "Usage: cp <src> <dst>");
        }
    }
    /* rm <path> */
    else if (strncmp(line, "rm ", 3) == 0) {
        const char *path = line + 3;
        while (*path == ' ') path++;
        cmd_rm(tty, path);
    }
    /* mkdir <path> */
    else if (strncmp(line, "mkdir ", 6) == 0) {
        const char *path = line + 6;
        while (*path == ' ') path++;
        cmd_mkdir(tty, path);
    }
    /* touch <path> */
    else if (strncmp(line, "touch ", 6) == 0) {
        const char *path = line + 6;
        while (*path == ' ') path++;
        cmd_touch(tty, path);
    }
    /* cat <path> */
    else if (strncmp(line, "cat ", 4) == 0) {
        const char *path = line + 4;
        while (*path == ' ') path++;
        cmd_cat(tty, path);
    }
    /* echo ... */
    else if (strncmp(line, "echo ", 5) == 0) {
        cmd_echo(tty, line + 5);
    }
    /* reboot */
    else if (strncmp(line, "reboot", 6) == 0) {
        cmd_reboot(tty);
    }
    /* help */
    else if (strncmp(line, "help", 4) == 0) {
        cmd_help(tty);
    }
    /* unknown */
    else {
        shell_println(tty, "unknown command. type 'help' for available commands.");
    }
}

/* ═══ Shell 任务入口 ═══ */

static void shell_task_main(void *arg)
{
    (void)arg;

    kern_fd_t tty = kern_open("/dev/ttyS0", KERN_O_RDWR);
    if (tty < 0) {
        return;  /* ttyS0 不可用 */
    }

    shell_println(tty, "Xeros Shell ready. Type 'help' for commands.");

    /*
     * 静态缓冲区：所有任务共享主栈，M5.update() 在两次调度 tick
     * 之间运行并可能覆盖栈上局部变量。将跨 tick 持久状态放在 BSS
     * 段可以避免此问题。
     */
    static char line[SHELL_BUF_SIZE];
    static size_t pos = 0;
    static char cwd[KERN_PATH_MAX] = "/";

    shell_print_prompt(tty, cwd);

    for (;;) {
        char ch;
        ssize_t n = kern_read(tty, &ch, 1);

        if (n <= 0) {
            kern_yield();
            continue;
        }

        /* 回显 */
        kern_write(tty, &ch, 1);

        if (ch == '\r' || ch == '\n') {
            line[pos] = '\0';
            shell_print(tty, "\r\n");
            shell_dispatch(tty, cwd, sizeof(cwd), line);
            shell_print_prompt(tty, cwd);
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
    kern_spawn("shell", shell_task_main, NULL, 4096);
}
