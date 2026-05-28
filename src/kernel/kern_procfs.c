/**
 * @file   kern_procfs.c
 * @brief  Xeros /proc 虚拟文件系统实现
 * @details 实现 kern_procfs_init()，将 /proc/tasks、/proc/uptime、
 *          /proc/version 三个只读文件注册到 VFS。
 *
 *          所有 procfs 文件均为只读——内容在 read 时动态生成。
 *          使用 inode->private_data 区分文件类型。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_procfs.h"
#include "kern_vfs.h"
#include "kern_task.h"
#include "kern_init.h"
#include "kern_version.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#ifndef NATIVE_TEST
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_heap_caps.h>
#endif

/* ═══ 内部常量 ═══ */

#define PROCFS_CONTENT_MAX  1024

/* ═══ procfs 文件类型枚举（存入 inode->private_data） ═══ */

typedef enum {
    KERN_PROCFS_TASKS     = 1,
    KERN_PROCFS_UPTIME    = 2,
    KERN_PROCFS_VERSION   = 3,
    KERN_PROCFS_MEMINFO   = 4,
    KERN_PROCFS_DEVELOPER = 5,
} kern_procfs_file_type_t;

/* ═══ 内部状态 ═══ */

static bool g_procfs_initialized = false;

/* ═══ 前向声明 ═══ */

static ssize_t procfs_read(kern_file_t *f, char *buf, size_t len);
static ssize_t procfs_write(kern_file_t *f, const char *buf, size_t len);

/* ═══ 文件操作表 ═══ */

static kern_file_ops_t g_procfs_fops = {
    .read    = procfs_read,
    .write   = procfs_write,
    .ioctl   = NULL,
    .release = NULL,
};

/* ═══ 任务状态 → 字符串 ═══ */

static const char *task_state_str(kern_task_state_t state)
{
    switch (state) {
    case KERN_TASK_READY:    return "READY";
    case KERN_TASK_RUNNING:  return "RUNNING";
    case KERN_TASK_SLEEPING: return "SLEEPING";
    case KERN_TASK_BLOCKED:  return "BLOCKED";
    case KERN_TASK_ZOMBIE:   return "ZOMBIE";
    default:                 return "UNKNOWN";
    }
}

/* ═══ 内容生成函数 ═══ */

/**
 * @brief 生成 /proc/tasks 内容
 * @return 写入 content 的字节数（不含终止符）
 */
static size_t procfs_tasks_generate(char *content, size_t max_len)
{
    size_t pos = 0;
    kern_task_t *t = kern_task_list_head();

    while (t != NULL && pos < max_len) {
        int written = snprintf(content + pos, max_len - pos,
                               "%d %s %s %zu/%zu\n",
                               t->pid,
                               t->name,
                               task_state_str(t->state),
                               kern_task_stack_usage(t),
                               t->stack_size);
        if (written < 0 || (size_t)written >= max_len - pos) {
            break;
        }
        pos += (size_t)written;
        t = t->next;
    }
    return pos;
}

/**
 * @brief 生成 /proc/uptime 内容
 * @note  使用 esp_timer_get_time()（ESP32）或时钟计数（Native）获取实际运行时间
 */
static size_t procfs_uptime_generate(char *content, size_t max_len)
{
#ifndef NATIVE_TEST
    int64_t us = esp_timer_get_time();
    uint32_t seconds = (uint32_t)(us / 1000000);
    int written = snprintf(content, max_len, "%" PRIu32 ".%02" PRIu32 "\n",
                           seconds, (uint32_t)((us / 10000) % 100));
#else
    int written = snprintf(content, max_len, "0\n");
#endif
    return (written > 0) ? (size_t)written : 0;
}

/**
 * @brief 生成 /proc/version 内容
 * @note  使用 kern_version.h 统一定义的版本号和开发者信息
 */
static size_t procfs_version_generate(char *content, size_t max_len)
{
    int written = snprintf(content, max_len,
        "Xeros " XEROS_VERSION_STRING " (" XEROS_CODENAME ")\n"
        "Developer: " XEROS_DEVELOPER "\n"
        "Platform: " XEROS_PLATFORM "\n"
        "Compiled: " __DATE__ " " __TIME__ "\n");
    return (written > 0) ? (size_t)written : 0;
}

/**
 * @brief 生成 /proc/meminfo 内容（新增）
 * @note  显示堆内存使用情况
 */
static size_t procfs_meminfo_generate(char *content, size_t max_len)
{
#ifndef NATIVE_TEST
    uint32_t free_heap  = esp_get_free_heap_size();
    uint32_t min_free   = esp_get_minimum_free_heap_size();
    uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    uint32_t used_heap  = total_heap - free_heap;

    int written = snprintf(content, max_len,
                           "MemTotal: %" PRIu32 " kB\n"
                           "MemFree:  %" PRIu32 " kB\n"
                           "MemUsed:  %" PRIu32 " kB\n"
                           "MinFree:  %" PRIu32 " kB\n",
                           total_heap / 1024,
                           free_heap / 1024,
                           used_heap / 1024,
                           min_free / 1024);
#else
    int written = snprintf(content, max_len, "MemTotal: N/A (native)\n");
#endif
    return (written > 0) ? (size_t)written : 0;
}

/**
 * @brief 生成 /proc/developer 内容（新增）
 */
static size_t procfs_developer_generate(char *content, size_t max_len)
{
    int written = snprintf(content, max_len,
        "Developer: " XEROS_DEVELOPER "\n"
        "Project: " XEROS_CODENAME " (Xerintosh UI)\n");
    return (written > 0) ? (size_t)written : 0;
}

/* ═══ 文件操作实现 ═══ */

static ssize_t procfs_read(kern_file_t *f, char *buf, size_t len)
{
    if (f == NULL || buf == NULL || len == 0) {
        return KERN_EINVAL;
    }

    char content[PROCFS_CONTENT_MAX];
    size_t content_len = 0;

    kern_procfs_file_type_t type =
        (kern_procfs_file_type_t)(uintptr_t)f->inode->private_data;

    switch (type) {
    case KERN_PROCFS_TASKS:
        content_len = procfs_tasks_generate(content, sizeof(content));
        break;
    case KERN_PROCFS_UPTIME:
        content_len = procfs_uptime_generate(content, sizeof(content));
        break;
    case KERN_PROCFS_VERSION:
        content_len = procfs_version_generate(content, sizeof(content));
        break;
    case KERN_PROCFS_MEMINFO:
        content_len = procfs_meminfo_generate(content, sizeof(content));
        break;
    case KERN_PROCFS_DEVELOPER:
        content_len = procfs_developer_generate(content, sizeof(content));
        break;
    default:
        return KERN_EINVAL;
    }

    /* EOF */
    if (f->f_pos >= content_len) {
        return 0;
    }

    /* 从当前位置拷贝 */
    size_t available = content_len - f->f_pos;
    size_t to_copy = (available < len) ? available : len;
    memcpy(buf, content + f->f_pos, to_copy);
    f->f_pos += to_copy;
    return (ssize_t)to_copy;
}

static ssize_t procfs_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;
    (void)buf;
    (void)len;
    return KERN_EACCES;  /* /proc 文件只读 */
}

/* ═══ 内部：注册单个 procfs 文件 ═══ */

static int procfs_register_file(const char *name, kern_procfs_file_type_t type)
{
    char path[KERN_PATH_MAX];
    int written = snprintf(path, sizeof(path), "/proc/%s", name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return KERN_ENOSPC;
    }

    kern_inode_t *inode = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
    if (inode == NULL) {
        return KERN_ENOMEM;
    }

    inode->type         = KERN_FILE_REGULAR;
    inode->fops         = &g_procfs_fops;
    inode->private_data = (void *)(uintptr_t)type;

    int rc = kern_dentry_register(path, inode);
    if (rc != KERN_OK) {
        free(inode);
        return rc;
    }

    return KERN_OK;
}

/* ═══ 初始化 ═══ */

void kern_procfs_init(void)
{
    if (g_procfs_initialized) {
        return;
    }

    /* 确保 VFS 已初始化 */
    kern_vfs_init();

    /* 创建 /proc 目录 */
    kern_vfs_mkdir("/proc");

    /* 注册文件 */
    int rc;
    rc = procfs_register_file("tasks", KERN_PROCFS_TASKS);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_WARN, "procfs: failed to register /proc/tasks");
    }
    rc = procfs_register_file("uptime", KERN_PROCFS_UPTIME);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_WARN, "procfs: failed to register /proc/uptime");
    }
    rc = procfs_register_file("version", KERN_PROCFS_VERSION);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_WARN, "procfs: failed to register /proc/version");
    }
    rc = procfs_register_file("meminfo", KERN_PROCFS_MEMINFO);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_WARN, "procfs: failed to register /proc/meminfo");
    }
    rc = procfs_register_file("developer", KERN_PROCFS_DEVELOPER);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_WARN, "procfs: failed to register /proc/developer");
    }

    g_procfs_initialized = true;
    kern_log(KERN_LOG_INFO, "procfs initialized");
}
