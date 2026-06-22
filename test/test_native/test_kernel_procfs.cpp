/**
 * @file   test_kernel_procfs.cpp
 * @brief  Xeros /proc 虚拟文件系统（procfs）单元测试
 * @details 测试 /proc/tasks、/proc/uptime、/proc/version 三个静态文件的
 *          打开、读取、只读属性，以及不存在文件的错误处理。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_init.h"
#include "kernel/kern_task.h"
#include "kernel/kern_procfs.h"
}

/* ═══ 测试辅助：设置 procfs 运行环境 ═══ */

static void procfs_setup(void)
{
    kern_vfs_init();
    kern_sched_init();   /* 创建 idle 任务，供 /proc/tasks 遍历 */
    kern_procfs_init();
}

/* ═══ 初始化测试 ═══ */

TEST(KernelProcFSTest, ProcFsInitDoesNotCrash)
{
    kern_vfs_init();
    kern_sched_init();
    kern_procfs_init();
    SUCCEED();
}

/* ═══ /proc/tasks 测试 ═══ */

TEST(KernelProcFSTest, TasksFileExists)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/tasks", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelProcFSTest, TasksFileReadable)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/tasks", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[512];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "idle"), nullptr);

    kern_close(fd);
}

TEST(KernelProcFSTest, TasksFileNotEmpty)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/tasks", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[512];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);
    EXPECT_GT(strlen(buf), (size_t)0);

    kern_close(fd);
}

/* ═══ /proc/version 测试 ═══ */

TEST(KernelProcFSTest, VersionFileReadable)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/version", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[128];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "Xeros"), nullptr);

    kern_close(fd);
}

/* ═══ /proc/uptime 测试 ═══ */

TEST(KernelProcFSTest, UptimeFileReadable)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/uptime", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[128];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);

    kern_close(fd);
}

/* ═══ 只读属性测试 ═══ */

TEST(KernelProcFSTest, ProcfsFilesAreReadOnly)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/tasks", KERN_O_WRONLY);
    /* 以只写方式打开一个只有读 fops 的文件，写入应失败 */
    if (fd >= 0) {
        ssize_t n = kern_write(fd, "test", 4);
        EXPECT_LT(n, 0);
        kern_close(fd);
    }
    /* 如果 open 本身失败（因为权限），也算只读 */
}

/* ═══ 不存在的文件测试 ═══ */

TEST(KernelProcFSTest, NonexistentProcFile)
{
    procfs_setup();

    kern_fd_t fd = kern_open("/proc/nonexistent", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}
