/**
 * @file   kern_shell_cmds.c
 * @brief  Xeros Shell 命令实现
 * @details 包含全部 Shell 命令处理函数和命令表。
 *          文件命令依赖 kern_shell_cmds_internal.h 声明。
 *
 *          命令表化设计：每条命令通过 kern_shell_cmd_t 注册，命令名精确匹配。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_shell_cmds.h"
#include "kern_vfs.h"
#include "kern_task.h"
#include "kern_types.h"
#include "kern_kmalloc.h"
#include "kern_init.h"
#include "kern_version.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#ifndef NATIVE_TEST
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#endif

#include "kern_shell_cmds_internal.h"
#include "app/storage/storage.h"

#ifndef NATIVE_TEST
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#else
#endif

/* ═══ 输出辅助 ═══ */

void kern_shell_print(kern_fd_t tty, const char *msg)
{
    if (tty >= 0 && msg != NULL) {
        kern_write(tty, msg, strlen(msg));
    }
}

void kern_shell_println(kern_fd_t tty, const char *msg)
{
    kern_shell_print(tty, msg);
    kern_shell_print(tty, "\r\n");
}

/* ═══ 路径解析 ═══ */

/**
 * @brief 根据属性名构造 sysfs 文件路径
 * @details log_level 位于 /sys/kernel/，其余属性位于 /sys/ 根目录。
 */
static void kern_shell_sysfs_attr_path(const char *attr, char *out, size_t out_size)
{
    if (attr != NULL && strcmp(attr, "log_level") == 0) {
        snprintf(out, out_size, "/sys/kernel/%s", attr ? attr : "");
    } else {
        snprintf(out, out_size, "/sys/%s", attr ? attr : "");
    }
}

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
    bool cwd_is_root = (cwd[0] == '/' && cwd[1] == '\0');
    int written = snprintf(out, out_size, "%s%s%s",
                           cwd, cwd_is_root ? "" : "/", arg);
    if (written < 0 || (size_t)written >= out_size) return NULL;
    return out;
}

/* ═══ 命令历史（环形缓冲区）═══ */

#define HISTORY_SIZE 8

static char g_history[HISTORY_SIZE][64]; /* 最多 8 条命令，各 64 字节 */
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

    strncpy(g_history[g_hist_index], cmd, 63);
    g_history[g_hist_index][63] = '\0';
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

/* ═══ ps — 列出所有任务 ═══ */

static void cmd_ps(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;

    kern_task_t *task = kern_task_list_head();

    kern_shell_println(tty, "PID  STATE     NAME           STACK");

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

        /*
         * 手动左对齐格式化。
         * PID 左对齐 4 字符，名称按终端显示宽度左对齐 14 列。
         */
        /* PID: 左对齐 4 字符 */
        char pid_str[8];
        int pid_len = snprintf(pid_str, sizeof(pid_str), "%d", (int)task->pid);
        kern_shell_print(tty, pid_str);
        for (int p = pid_len; p < 4; p++) kern_shell_print(tty, " ");
        kern_shell_print(tty, " ");

        kern_shell_print(tty, state_str);
        kern_shell_print(tty, " ");

        /* 名称: 按终端显示宽度左对齐（ASCII=1列, 多字节UTF-8=2列） */
        const char *nm = task->name[0] != '\0' ? task->name : "?";
        kern_shell_print(tty, nm);
        int dw = 0;  /* 显示宽度 */
        for (const char *s = nm; *s != '\0'; ) {
            if ((*s & 0x80) == 0)      { dw++;  s++; }
            else if ((*s & 0xE0) == 0xC0) { dw += 2; s += 2; }
            else                       { dw += 2; s += 3; }  /* 中/日/韩 */
        }
        for (int n = dw; n < 14; n++) kern_shell_print(tty, " ");
        kern_shell_print(tty, " ");

        /* 栈: 虚任务无独立栈，实任务显示 已用/总量（字节） */
        {
#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)
            /* Native: 栈由 Xeros 管理（字节），无 port_thread 字段 */
            size_t used = kern_task_stack_usage(task);
            size_t total = task->stack_size;
            char stack_buf[48];
            snprintf(stack_buf, sizeof(stack_buf), "%zu/%zu/%zu",
                     used, total, kern_task_stack_highwater(task));
            kern_shell_println(tty, stack_buf);
#else
            if (task->port_thread == KERN_PORT_THREAD_NULL) {
                kern_shell_println(tty, "shrd");  /* 共享 ui 线程栈 */
            } else {
                size_t used = kern_task_stack_usage(task);
                size_t total = task->stack_size;  /* 已统一为字节 */
                char stack_buf[48];
                snprintf(stack_buf, sizeof(stack_buf), "%zu/%zu/%zu",
                         used, total, kern_task_stack_highwater(task));
                kern_shell_println(tty, stack_buf);
            }
#endif
        }

        kern_task_t *next = task->next;  /* 先保存 next 再 yield */
        kern_yield();
        task = next;
    }
}

/* ═══ ls — 列出目录 ═══ */

static void cmd_ls(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    const char *path_arg = (argc > 1) ? argv[1] : NULL;

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, path_arg, abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "ls: path too long"); return; }

    kern_dentry_t *dir = kern_path_resolve(target);
    if (dir == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "ls: cannot access '%s': no such entry", target);
        kern_shell_println(tty, err);
        return;
    }

    if (dir->inode != NULL && dir->inode->type != KERN_FILE_DIR) {
        kern_shell_println(tty, target);
        return;
    }

    char header[80];
    snprintf(header, sizeof(header), "%s (%u entries):", target, dir->child_count);
    kern_shell_println(tty, header);

    for (uint16_t i = 0; i < dir->child_count; i++) {
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
        kern_shell_println(tty, line);
        kern_yield();  /* 防止长时间列表触发看门狗 */
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
    if (target == NULL) { kern_shell_println(tty, "cd: path too long"); return; }

    kern_dentry_t *dir = kern_path_resolve(target);
    if (dir == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "cd: '%s': no such directory", target);
        kern_shell_println(tty, err);
        return;
    }
    if (dir->inode != NULL && dir->inode->type != KERN_FILE_DIR) {
        char err[64];
        snprintf(err, sizeof(err), "cd: '%s': not a directory", target);
        kern_shell_println(tty, err);
        return;
    }

    strncpy(cwd, target, cwd_size);
    cwd[cwd_size - 1] = '\0';
}

/* ═══ pwd — 打印工作目录 ═══ */

static void cmd_pwd(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd_size;
    kern_shell_println(tty, cwd);
}

/* ═══ cat — 读取文件 ═══ */

static void cmd_cat(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: cat <path>"); return; }

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, argv[1], abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "cat: path too long"); return; }

    kern_fd_t fd = kern_open(target, KERN_O_RDONLY);
    if (fd < 0) {
        char err[64];
        snprintf(err, sizeof(err), "cat: cannot open '%s'", target);
        kern_shell_println(tty, err);
        return;
    }

    char buf[128];
    ssize_t n;
    int iter = 0;
    size_t total_read = 0;
    while ((n = kern_read(fd, buf, sizeof(buf))) > 0) {
        kern_write(tty, buf, (size_t)n);
        total_read += (size_t)n;
        kern_yield();  /* 防止大文件读取触发看门狗 */

        /* 防御性上限：防止异常设备返回恒正数据导致无限循环。
         * 128B × 1024 = 128KB，远超任何正常 procfs/sysfs/touch 文件。 */
        if (++iter >= 1024) {
            kern_shell_print(tty, "\r\ncat: max read limit reached\r\n");
            break;
        }
    }

    /* 首读即返回 0 或错误：告知用户文件存在但无可读数据
     * （对 /dev/input0、/dev/pwrkey 等事件流设备，无事件时 read 返回 0 是正常行为） */
    if (total_read == 0 && n < 0) {
        char err[48];
        snprintf(err, sizeof(err), "cat: read error (err=%d)\r\n", (int)n);
        kern_shell_print(tty, err);
    } else if (total_read == 0) {
        kern_shell_println(tty, "(no data)");
    }
    kern_close(fd);
}

/* ═══ cp — 复制文件 ═══ */

static void cmd_cp(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    if (argc < 3) { kern_shell_println(tty, "Usage: cp <src> <dst>"); return; }

    char src_abs[KERN_PATH_MAX];
    const char *src = resolve_path(cwd, argv[1], src_abs, sizeof(src_abs));
    if (src == NULL) { kern_shell_println(tty, "cp: source path too long"); return; }

    char dst_abs[KERN_PATH_MAX];
    const char *dst = resolve_path(cwd, argv[2], dst_abs, sizeof(dst_abs));
    if (dst == NULL) { kern_shell_println(tty, "cp: dest path too long"); return; }

    kern_fd_t fd_src = kern_open(src, KERN_O_RDONLY);
    if (fd_src < 0) { kern_shell_println(tty, "cp: cannot open source"); return; }

    kern_fd_t fd_dst = kern_open(dst, KERN_O_WRONLY);
    if (fd_dst < 0) { kern_close(fd_src); kern_shell_println(tty, "cp: cannot open destination"); return; }

    char buf[128];
    ssize_t n;
    while ((n = kern_read(fd_src, buf, sizeof(buf))) > 0) {
        kern_write(fd_dst, buf, (size_t)n);
        kern_yield();  /* 防止大文件复制触发看门狗 */
    }
    kern_close(fd_dst);
    kern_close(fd_src);
}

/* ═══ rm — 删除文件/目录 ═══ */

static void cmd_rm(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: rm <path>"); return; }

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, argv[1], abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "rm: path too long"); return; }

    int ret = kern_vfs_unlink(target);
    if (ret == KERN_ENOENT) {
        kern_shell_println(tty, "rm: no such file or directory");
    } else if (ret == KERN_ENOTEMPTY) {
        kern_shell_println(tty, "rm: directory not empty");
    } else if (ret != KERN_OK) {
        kern_shell_println(tty, "rm: failed");
    }
}

/* ═══ mkdir — 创建目录 ═══ */

static void cmd_mkdir(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: mkdir <path>"); return; }

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, argv[1], abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "mkdir: path too long"); return; }

    int ret = kern_vfs_mkdir(target);
    if (ret == KERN_EEXIST) {
        kern_shell_println(tty, "mkdir: directory already exists");
    } else if (ret != KERN_OK) {
        kern_shell_println(tty, "mkdir: failed");
    }
}

/* ═══ touch — 创建空文件 ═══ */

static void cmd_touch(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: touch <path>"); return; }

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, argv[1], abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "touch: path too long"); return; }

    int ret = kern_vfs_touch(target);
    if (ret == KERN_EEXIST) {
        /* 已存在，静默成功 */
    } else if (ret != KERN_OK) {
        kern_shell_println(tty, "touch: failed");
    }
}

/* ═══ echo — 输出文本 / 重定向写入 ═══ */

static void cmd_echo(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;

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
        char abs_path[KERN_PATH_MAX];
        const char *target = resolve_path(cwd, argv[redirect_pos + 1], abs_path, sizeof(abs_path));
        if (target == NULL) { kern_shell_println(tty, "echo: path too long"); return; }

        kern_fd_t fd = kern_open(target, KERN_O_WRONLY);
        if (fd < 0) {
            kern_shell_println(tty, "echo: cannot write to file");
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
            if (i > 1) kern_shell_print(tty, " ");
            kern_shell_print(tty, argv[i]);
        }
        kern_shell_print(tty, "\r\n");
    }
}

/* ═══ reboot — 重启设备 ═══ */

static void cmd_reboot(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    kern_shell_println(tty, "Rebooting...");
    { volatile uint32_t s = 0; while (s < 500000) s++; }
#ifndef NATIVE_TEST
    esp_restart();
#else
    kern_shell_println(tty, "(native: reboot not available)");
#endif
}

/* ═══ help — 帮助 ═══ */

static void cmd_help(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    kern_shell_println(tty, "Xeros Shell — available commands:");

    const kern_shell_cmd_t *cmds = kern_shell_get_builtin_cmds();
    int count = kern_shell_get_builtin_count();
    char line[96];
    for (int i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "  %s - %s", cmds[i].name, cmds[i].help);
        kern_shell_println(tty, line);
        kern_yield();  /* 防止长命令列表触发看门狗 */
    }
}

/* ═══ free — 堆内存统计（新增）═══ */

static void cmd_free(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[96];
    kern_kmem_stat_t st;
    kern_kmem_get_stats(&st);

#ifndef NATIVE_TEST
    snprintf(line, sizeof(line),
             "free=%" PRIu32 " max_alloc=%" PRIu32 " min_free=%" PRIu32 " frag=%" PRIu32 "%% reserved=%" PRIu32 " pressure=%d",
             (uint32_t)st.free_bytes,
             (uint32_t)st.largest_free_block,
             (uint32_t)st.min_free_bytes,
             (uint32_t)st.fragmentation_percent,
             (uint32_t)kern_kmem_reserved_bytes(),
             (int)kern_kmem_pressure_level());
#else
    snprintf(line, sizeof(line),
             "allocated=%" PRIu32 " reserved=%" PRIu32 " pressure=%d (native mode)",
             (uint32_t)st.allocated_bytes,
             (uint32_t)kern_kmem_reserved_bytes(),
             (int)kern_kmem_pressure_level());
#endif
    kern_shell_println(tty, line);
}

/* ═══ kill — 终止任务（新增）═══ */

static void cmd_kill(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: kill <pid>"); return; }

    /* 解析 PID */
    long pid_l = 0;
    for (char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '9') { kern_shell_println(tty, "kill: invalid PID"); return; }
        pid_l = pid_l * 10 + (*p - '0');
    }

    kern_task_t *task = kern_task_get((kern_pid_t)pid_l);
    if (task == NULL) {
        kern_shell_println(tty, "kill: no such task");
        return;
    }
    if (task->state == KERN_TASK_ZOMBIE) {
        kern_shell_println(tty, "kill: task already zombie");
        return;
    }
    if (kern_task_is_protected(task)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "kill: cannot kill system task '%s'", task->name);
        kern_shell_println(tty, msg);
        return;
    }

    /* 使用统一 kill API（处理虚任务注销 + FreeRTOS 线程销毁） */
    int ret = kern_task_kill((kern_pid_t)pid_l);
    char msg[64];
    if (ret == 0) {
        snprintf(msg, sizeof(msg), "task %ld (%s) killed", pid_l, task->name);
    } else {
        snprintf(msg, sizeof(msg), "kill: failed (err=%d)", ret);
    }
    kern_shell_println(tty, msg);
}

/* ═══ uname — 系统信息（新增）═══ */

static void cmd_uname(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[128];
    snprintf(line, sizeof(line), "Xeros " XEROS_VERSION_STRING " " XEROS_PLATFORM
             " compiled " __DATE__ " " __TIME__);
    kern_shell_println(tty, line);
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

    for (uint16_t i = 0; i < root->child_count; i++) {
        kern_yield();  /* 防止深层递归触发看门狗 */
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
    kern_shell_println(tty, line);
}

/* ═══ clear — 清屏（新增）═══ */

static void cmd_clear(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    kern_shell_print(tty, "\x1b[2J\x1b[H");  /* ANSI 清屏 + 光标归位 */
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
        kern_shell_println(tty, line);
    }
}

/* ═══ date — 运行时间（新增）═══ */

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
    kern_shell_println(tty, line);
#else
    kern_shell_println(tty, "Uptime: N/A (native mode)");
#endif
}

/* ═══ hexdump — 十六进制查看（新增）═══ */

static void cmd_hexdump(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: hexdump <path>"); return; }

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, argv[1], abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "hexdump: path too long"); return; }

    kern_fd_t fd = kern_open(target, KERN_O_RDONLY);
    if (fd < 0) {
        kern_shell_println(tty, "hexdump: cannot open file");
        return;
    }

    uint8_t buf[16];
    ssize_t n;
    uint32_t offset = 0;
    char line[80];

    while ((n = kern_read(fd, (char *)buf, sizeof(buf))) > 0) {
        /* 偏移 */
        int pos = snprintf(line, sizeof(line), "%08lx  ", offset);

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

        kern_shell_println(tty, line);
        offset += (uint32_t)n;
        kern_yield();  /* 防止大文件 hexdump 触发看门狗 */
    }
    kern_close(fd);
}

/* ═══ top — 实时任务监控（Phase 3 新增）═══ */

static void cmd_top(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv;
    kern_shell_println(tty, "TOP — press any key to exit");
    bool running = true;
    while (running) {
        cmd_ps(tty, 0, NULL, cwd, cwd_size);
        kern_shell_println(tty, "---");
        char c;
        if (kern_read(tty, &c, 1) > 0) running = false;
        if (running) {
            kern_sleep_ms(2000);  /* 使用内核 sleep API，兼容 FreeRTOS 和原生调度器 */
        }
    }
}

/* ═══ log — 日志级别查看/设置（Phase 3 新增）═══ */

static void cmd_log(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc > 1) {
        /* 设置日志级别：echo <n> > /sys/kernel/log_level */
        kern_fd_t fd = kern_open("/sys/kernel/log_level", KERN_O_WRONLY);
        if (fd < 0) { kern_shell_println(tty, "log: cannot write log_level"); return; }
        kern_write(fd, argv[1], strlen(argv[1]));
        kern_close(fd);
        kern_shell_println(tty, "OK");
    } else {
        /* 查看日志级别：cat /sys/kernel/log_level */
        cmd_cat(tty, 2, (char *[]){ "cat", "/sys/kernel/log_level", NULL }, cwd, cwd_size);
    }
}

/* ═══ param — 参数配置（Phase 3 新增）═══ */

static void cmd_param(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: param <list|get|set|save|load> [args]"); return; }

    if (strcmp(argv[1], "list") == 0) {
        /* 遍历已知 sysfs 属性；log_level 在 /sys/kernel/，其余在 /sys/ */
        const char *known[] = {
            "brightness", "rotation", "anim_speed", "anim_enabled", "log_level", NULL
        };
        for (int i = 0; known[i] != NULL; i++) {
            char path[KERN_PATH_MAX];
            kern_shell_sysfs_attr_path(known[i], path, sizeof(path));
            cmd_cat(tty, 2, (char *[]){ "cat", path, NULL }, cwd, cwd_size);
        }
    } else if (strcmp(argv[1], "get") == 0 && argc >= 3) {
        char path[KERN_PATH_MAX];
        kern_shell_sysfs_attr_path(argv[2], path, sizeof(path));
        cmd_cat(tty, 2, (char *[]){ "cat", path, NULL }, cwd, cwd_size);
    } else if (strcmp(argv[1], "set") == 0 && argc >= 4) {
        char path[KERN_PATH_MAX];
        kern_shell_sysfs_attr_path(argv[2], path, sizeof(path));
        kern_fd_t fd = kern_open(path, KERN_O_WRONLY);
        if (fd < 0) { kern_shell_println(tty, "param set: cannot open parameter"); return; }
        kern_write(fd, argv[3], strlen(argv[3]));
        kern_close(fd);
        kern_shell_println(tty, "OK");
    } else if (strcmp(argv[1], "save") == 0) {
        storage_save_all();
        kern_shell_println(tty, "OK: parameters saved");
    } else if (strcmp(argv[1], "load") == 0) {
        storage_load_all();
        kern_shell_println(tty, "OK: parameters loaded (reboot to apply hardware changes)");
    } else {
        kern_shell_println(tty, "param: unknown sub-command");
    }
}

/* ═══ bootloader — 进入 OTA 模式（Phase 3 新增）═══ */

static void cmd_bootloader(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    kern_shell_println(tty, "Entering bootloader mode...");
#ifndef NATIVE_TEST
    { volatile uint32_t s = 0; while (s < 500000) s++; }
    esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
    esp_restart();
#else
    kern_shell_println(tty, "(native: bootloader not available)");
#endif
}

/* ═══ factory — 恢复出厂设置（Phase 3 新增）═══ */

static void cmd_factory(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    kern_shell_println(tty, "WARNING: This will erase ALL settings and reboot.");
    kern_shell_print(tty, "Type 'yes' to confirm: ");
    char confirm[8] = {0};
    ssize_t n = kern_read(tty, confirm, sizeof(confirm) - 1);
    if (n <= 0) { kern_shell_println(tty, "\r\nAborted."); return; }
    confirm[n] = '\0';
    for (ssize_t i = n - 1; i >= 0 && (confirm[i] == '\r' || confirm[i] == '\n'); i--)
        confirm[i] = '\0';
    if (strcmp(confirm, "yes") != 0) { kern_shell_println(tty, "Aborted."); return; }
    kern_shell_println(tty, "\r\nErasing NVS and rebooting...");
#ifndef NATIVE_TEST
    nvs_flash_erase();
    esp_restart();
#else
    kern_shell_println(tty, "(native: factory reset not available)");
#endif
}

/* ═══ version — 固件/硬件信息（Phase 3 新增）═══ */

static void cmd_version(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[128];
    snprintf(line, sizeof(line), "Version: " XEROS_VERSION_STRING);
    kern_shell_println(tty, line);
    snprintf(line, sizeof(line), "Platform: " XEROS_PLATFORM);
    kern_shell_println(tty, line);
    snprintf(line, sizeof(line), "Build: " __DATE__ " " __TIME__);
    kern_shell_println(tty, line);
}

/* ═══ scope — 实时数据监测（Phase 3 新增）═══ */

scope_var_t g_scope_vars[SCOPE_MAX_VARS];
int         g_scope_count = 0;
bool        g_scope_running = false;
int         g_scope_period_ms = 1000;
uint64_t    g_scope_last_tick = 0;

void kern_shell_scope_tick(kern_fd_t tty)
{
    if (!g_scope_running || g_scope_count <= 0) return;
#ifndef NATIVE_TEST
    uint64_t now = esp_timer_get_time();
#else
    uint64_t now = 0;
    return;
#endif
    if (now - g_scope_last_tick < (uint64_t)g_scope_period_ms * 1000ULL) return;
    g_scope_last_tick = now;

    char line[256]; int pos = 0;
    for (int i = 0; i < g_scope_count; i++) {
        if (!g_scope_vars[i].active) continue;
        kern_fd_t fd = kern_open(g_scope_vars[i].path, KERN_O_RDONLY);
        if (fd >= 0) {
            char val[32]; ssize_t n2 = kern_read(fd, val, sizeof(val) - 1);
            if (n2 > 0) { val[n2] = '\0';
                pos += snprintf(line + pos, sizeof(line) - pos, "%s%s",
                               i > 0 ? "," : "", val); }
            kern_close(fd);
        }
    }
    if (pos > 0) kern_shell_println(tty, line);
}

static void cmd_scope(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: scope <add|start|stop> [args]"); return; }

    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        if (g_scope_count >= SCOPE_MAX_VARS) {
            kern_shell_println(tty, "scope: max variables reached"); return;
        }
        strncpy(g_scope_vars[g_scope_count].path, argv[2], KERN_PATH_MAX - 1);
        g_scope_vars[g_scope_count].active = true;
        g_scope_count++;
        kern_shell_println(tty, "OK");
    } else if (strcmp(argv[1], "start") == 0) {
        if (argc >= 3) g_scope_period_ms = atoi(argv[2]);
        g_scope_running = true;
        g_scope_last_tick = 0;
        kern_shell_println(tty, "scope started");
    } else if (strcmp(argv[1], "stop") == 0) {
        g_scope_running = false;
        kern_shell_println(tty, "scope stopped");
    } else {
        kern_shell_println(tty, "scope: unknown sub-command");
    }
}

/* ═══ mode — 运行模式（Phase 3 新增）═══ */

static void cmd_mode(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc >= 2 && strcmp(argv[1], "set") == 0 && argc >= 3) {
        kern_fd_t fd = kern_open("/sys/mode", KERN_O_WRONLY);
        if (fd < 0) { kern_shell_println(tty, "mode: /sys/mode not available"); return; }
        kern_write(fd, argv[2], strlen(argv[2]));
        kern_close(fd);
        kern_shell_println(tty, "OK");
    } else {
        cmd_cat(tty, 2, (char *[]){ "cat", "/sys/mode", NULL }, cwd, cwd_size);
    }
}

/* ═══ ctrl — 控制算法启停（Phase 3 新增）═══ */

static void cmd_ctrl(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc >= 2) {
        kern_fd_t fd = kern_open("/sys/ctrl", KERN_O_WRONLY);
        if (fd < 0) { kern_shell_println(tty, "ctrl: /sys/ctrl not available"); return; }
        kern_write(fd, argv[1], strlen(argv[1]));
        kern_close(fd);
        kern_shell_println(tty, "OK");
    } else {
        cmd_cat(tty, 2, (char *[]){ "cat", "/sys/ctrl", NULL }, cwd, cwd_size);
    }
}

/* ═══ info — 设备汇总信息（Phase 3 新增）═══ */

static void cmd_info(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    kern_shell_println(tty, "=== Device Info ===");
    cmd_uname(tty, 0, NULL, cwd, cwd_size);
    cmd_free(tty, 0, NULL, cwd, cwd_size);
    cmd_df(tty, 0, NULL, cwd, cwd_size);
    cmd_date(tty, 0, NULL, cwd, cwd_size);
}

/* ═══ io — GPIO 调试（Phase 3 新增）═══ */

static void cmd_io(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;
    if (argc < 2) { kern_shell_println(tty, "Usage: io <get|set> <pin> [value]"); return; }
    if (strcmp(argv[1], "get") == 0 && argc >= 3) {
        char path[KERN_PATH_MAX];
        snprintf(path, sizeof(path), "/sys/gpio/%s", argv[2]);
        cmd_cat(tty, 2, (char *[]){ "cat", path, NULL }, cwd, cwd_size);
    } else if (strcmp(argv[1], "set") == 0 && argc >= 4) {
        char path[KERN_PATH_MAX];
        snprintf(path, sizeof(path), "/sys/gpio/%s", argv[2]);
        kern_fd_t fd = kern_open(path, KERN_O_WRONLY);
        if (fd < 0) { kern_shell_println(tty, "io: pin not available"); return; }
        kern_write(fd, argv[3], strlen(argv[3]));
        kern_close(fd);
        kern_shell_println(tty, "OK");
    } else {
        kern_shell_println(tty, "io: unknown sub-command");
    }
}

/* ═══ dskey — DeepSeek API Key 管理 ═══ */

static void cmd_dskey(kern_fd_t tty, int argc, char *argv[],
                      char *cwd, size_t cwd_size)
{
    (void)cwd; (void)cwd_size;

    if (argc < 2) {
        /* 显示当前 key（脱敏） */
        char key[STORAGE_API_KEY_MAX_LEN];
        if (storage_get_deepseek_key(key, sizeof(key)) && key[0] != '\0') {
            size_t len = strlen(key);
            kern_shell_print(tty, "Current key: ");
            if (len <= 8) {
                kern_shell_println(tty, "****");
            } else {
                /* 显示前 4 位 + **** + 后 4 位 */
                char masked[64];
                snprintf(masked, sizeof(masked), "%.4s****%.4s", key, key + len - 4);
                kern_shell_println(tty, masked);
            }
        } else {
            kern_shell_println(tty, "No key configured.");
        }
        kern_shell_println(tty, "Usage: dskey <api_key>");
        return;
    }

    storage_set_deepseek_key(argv[1]);
    kern_shell_println(tty, "DeepSeek API key saved. Reboot or re-enter Token Usage to apply.");
}

/* ═══ meminfo — 详细内存信息 ═══ */

static void cmd_meminfo(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[128];
    kern_kmem_stat_t st;
    kern_kmem_get_stats(&st);

#ifndef NATIVE_TEST
    snprintf(line, sizeof(line), "Total DRAM:    %" PRIu32 " bytes", (uint32_t)st.total_bytes);
    kern_shell_println(tty, line);
    snprintf(line, sizeof(line), "Free DRAM:     %" PRIu32 " bytes", (uint32_t)st.free_bytes);
    kern_shell_println(tty, line);
    snprintf(line, sizeof(line), "Largest block: %" PRIu32 " bytes", (uint32_t)st.largest_free_block);
    kern_shell_println(tty, line);
    snprintf(line, sizeof(line), "Min free ever: %" PRIu32 " bytes", (uint32_t)st.min_free_bytes);
    kern_shell_println(tty, line);
    uint32_t iram_free = heap_caps_get_free_size(MALLOC_CAP_32BIT) - heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snprintf(line, sizeof(line), "IRAM free:     %" PRIu32 " bytes", iram_free);
    kern_shell_println(tty, line);
    snprintf(line, sizeof(line), "Fragmentation: %" PRIu32 "%%", (uint32_t)st.fragmentation_percent);
    kern_shell_println(tty, line);
#else
    snprintf(line, sizeof(line), "Allocated: %" PRIu32 " bytes (native mode)", (uint32_t)st.allocated_bytes);
    kern_shell_println(tty, line);
#endif
}

/* ═══ tree — 递归目录树 ═══ */

static void tree_recurse(kern_fd_t tty, kern_dentry_t *dir, int depth, const char *prefix)
{
    if (dir == NULL) return;

    for (uint16_t i = 0; i < dir->child_count; i++) {
        kern_dentry_t *child = dir->children[i];
        if (child == NULL) continue;

        bool is_last = (i == dir->child_count - 1);
        const char *branch = is_last ? "└── " : "├── ";
        const char *cont   = is_last ? "    " : "│   ";

        /* 构建缩进前缀 */
        char indent[64];
        int off = 0;
        if (depth > 0) {
            off = snprintf(indent, sizeof(indent), "%s", prefix);
        }
        snprintf(indent + off, sizeof(indent) - off, "%s%s",
                 branch, child->name);
        kern_shell_println(tty, indent);

        kern_yield();  /* 防止深层递归触发看门狗 */

        /* 目录则递归 */
        if (child->inode != NULL && child->inode->type == KERN_FILE_DIR) {
            char new_prefix[64];
            if (depth > 0) {
                snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, cont);
            } else {
                snprintf(new_prefix, sizeof(new_prefix), "%s", cont);
            }
            tree_recurse(tty, child, depth + 1, new_prefix);
        }
    }
}

static void cmd_tree(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)cwd_size;
    const char *path_arg = (argc > 1) ? argv[1] : NULL;

    char abs_path[KERN_PATH_MAX];
    const char *target = resolve_path(cwd, path_arg, abs_path, sizeof(abs_path));
    if (target == NULL) { kern_shell_println(tty, "tree: path too long"); return; }

    kern_dentry_t *root = kern_path_resolve(target);
    if (root == NULL) {
        kern_shell_println(tty, "tree: no such directory");
        return;
    }

    kern_shell_println(tty, target);
    tree_recurse(tty, root, 0, "");
}

/* ═══ 内置命令表 ═══ */

static const kern_shell_cmd_t g_builtin_cmds[] = {
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

    /* ── Phase 3 新增命令 ── */
    { "top",       cmd_top,       "real-time task monitor" },
    { "mem",       cmd_free,      "show heap memory (alias: free)" },
    { "log",       cmd_log,       "view/set log level" },
    { "param",     cmd_param,     "config parameters (list/get/set/save/load)" },
    { "bootloader",cmd_bootloader,"enter OTA bootloader mode" },
    { "factory",   cmd_factory,   "factory reset (DANGER!)" },
    { "version",   cmd_version,   "firmware & hardware info" },
    { "scope",     cmd_scope,     "real-time data scope (add/start/stop)" },
    { "mode",      cmd_mode,      "view/set run mode" },
    { "ctrl",      cmd_ctrl,      "control algorithm start/stop/reset" },
    { "info",      cmd_info,      "device summary info" },
    { "io",        cmd_io,        "GPIO debug (get/set <pin> [value])" },

    /* ── App 配置命令 ── */
    { "dskey",     cmd_dskey,     "set/view DeepSeek API key" },

    /* ── 新增 Shell 增强命令 ── */
    { "meminfo",   cmd_meminfo,   "detailed memory info (DRAM/IRAM)" },
    { "tree",      cmd_tree,      "recursive directory tree" },
    { "tasks",     cmd_ps,        "list tasks (alias: ps)" },
    { "uptime",    cmd_date,      "show uptime (alias: date)" },
};

#define BUILTIN_COUNT (sizeof(g_builtin_cmds) / sizeof(g_builtin_cmds[0]))

/* ═══ 命令查找 ═══ */

const kern_shell_cmd_t *kern_shell_get_builtin_cmds(void)
{
    return g_builtin_cmds;
}

int kern_shell_get_builtin_count(void)
{
    return (int)BUILTIN_COUNT;
}

const kern_shell_cmd_t *kern_shell_lookup_cmd(const char *name)
{
    if (name == NULL || name[0] == '\0') return NULL;

    /* 查内置命令 */
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(g_builtin_cmds[i].name, name) == 0) {
            return &g_builtin_cmds[i];
        }
    }

    return NULL;
}

void kern_shell_exec_cmd(kern_fd_t tty, int argc, char *argv[],
                   char *cwd, size_t cwd_size)
{
    if (argc <= 0 || argv == NULL || argv[0] == NULL) return;

    const kern_shell_cmd_t *cmd = kern_shell_lookup_cmd(argv[0]);
    if (cmd == NULL) {
        char err[64];
        snprintf(err, sizeof(err), "command not found: %s", argv[0]);
        kern_shell_println(tty, err);
        return;
    }

    cmd->handler(tty, argc, argv, cwd, cwd_size);
}
