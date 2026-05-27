/**
 * @file   test_kernel_syscall.cpp
 * @brief  Xeros 系统调用接口单元测试
 * @details 测试 syscall 编号常量、分发器正确性、用户态封装函数。
 *          覆盖所有 SYS_OPEN ~ SYS_SPAWN 以及未知编号的错误处理。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_task.h"
#include "kernel/kern_ipc.h"
#include "kernel/kern_syscall.h"
#include "kernel/kern_init.h"
#include "kernel/kern_devfs.h"
#include "kernel/devices/kern_devices.h"
}

/* ═══ 测试环境 ═══ */

class KernelSyscallTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        kern_init();
        kern_vfs_init();
        kern_devfs_init();
        kern_devices_init();
    }
};

/* ═══ Syscall 编号常量检查 ═══ */

TEST_F(KernelSyscallTest, SyscallNumbersAreSequential)
{
    EXPECT_EQ(SYS_OPEN,  0);
    EXPECT_EQ(SYS_CLOSE, 1);
    EXPECT_EQ(SYS_READ,  2);
    EXPECT_EQ(SYS_WRITE, 3);
    EXPECT_EQ(SYS_IOCTL, 4);
    EXPECT_EQ(SYS_YIELD, 5);
    EXPECT_EQ(SYS_SLEEP, 6);
    EXPECT_EQ(SYS_EXIT,  7);
    EXPECT_EQ(SYS_PIPE,  8);
    EXPECT_EQ(SYS_SPAWN, 9);
    EXPECT_EQ(SYS_MAX,  10);
}

TEST_F(KernelSyscallTest, UnknownSyscallReturnsError)
{
    long ret = kern_syscall(999, 0, 0, 0, 0);
    EXPECT_EQ(ret, (long)KERN_EINVAL);
}

TEST_F(KernelSyscallTest, UnknownSyscallNegative)
{
    long ret = kern_syscall(-1, 0, 0, 0, 0);
    EXPECT_EQ(ret, (long)KERN_EINVAL);
}

/* ═══ 文件操作 syscall（SYS_OPEN/SYS_CLOSE/SYS_READ/SYS_WRITE）═══ */

TEST_F(KernelSyscallTest, SysOpenNullReturnsValidFd)
{
    kern_fd_t fd = sys_open("/dev/null", KERN_O_WRONLY);
    EXPECT_GE(fd, 0);
    sys_close(fd);
}

TEST_F(KernelSyscallTest, SysOpenNonexistentReturnsError)
{
    kern_fd_t fd = sys_open("/nonexistent/file", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}

TEST_F(KernelSyscallTest, SysCloseInvalidFdReturnsError)
{
    int ret = sys_close(-1);
    EXPECT_LT(ret, 0);
}

TEST_F(KernelSyscallTest, SysWriteToDevNull)
{
    kern_fd_t fd = sys_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    const char *data = "hello syscall";
    ssize_t n = sys_write(fd, data, strlen(data));
    EXPECT_EQ(n, (ssize_t)strlen(data));

    sys_close(fd);
}

TEST_F(KernelSyscallTest, SysWriteToClosedFdFails)
{
    kern_fd_t fd = sys_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    sys_close(fd);

    ssize_t n = sys_write(fd, "x", 1);
    EXPECT_LT(n, 0);
}

TEST_F(KernelSyscallTest, SysReadFromWriteOnlyFails)
{
    kern_fd_t fd = sys_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sys_read(fd, buf, sizeof(buf));
    /* /dev/null 不支持 read */
    EXPECT_LE(n, 0);

    sys_close(fd);
}

/* ═══ sys_ioctl 测试 ═══ */

TEST_F(KernelSyscallTest, SysIoctlInvalidFdFails)
{
    int ret = sys_ioctl(-1, 0, 0);
    EXPECT_LT(ret, 0);
}

/* ═══ sys_yield / sys_sleep_ms 测试 ═══ */

TEST_F(KernelSyscallTest, SysYieldDoesNotCrash)
{
    sys_yield();
    SUCCEED();
}

TEST_F(KernelSyscallTest, SysSleepMsDoesNotCrash)
{
    sys_sleep_ms(1);
    SUCCEED();
}

/* ═══ sys_pipe 测试 ═══ */

TEST_F(KernelSyscallTest, SysPipeCreatesValidFds)
{
    kern_fd_t fds[2] = {KERN_FD_INVALID, KERN_FD_INVALID};
    int ret = sys_pipe(fds);

    EXPECT_EQ(ret, KERN_OK);
    EXPECT_GE(fds[0], 0);
    EXPECT_GE(fds[1], 0);
    EXPECT_NE(fds[0], fds[1]);

    sys_close(fds[0]);
    sys_close(fds[1]);
}

TEST_F(KernelSyscallTest, SysPipeReadWriteRoundTrip)
{
    kern_fd_t fds[2];
    int ret = sys_pipe(fds);
    ASSERT_EQ(ret, KERN_OK);

    const char *msg = "pipe through syscall";
    ssize_t nw = sys_write(fds[1], msg, strlen(msg));
    EXPECT_EQ(nw, (ssize_t)strlen(msg));

    char buf[64] = {0};
    ssize_t nr = sys_read(fds[0], buf, sizeof(buf));
    EXPECT_EQ(nr, (ssize_t)strlen(msg));
    EXPECT_STREQ(buf, msg);

    sys_close(fds[0]);
    sys_close(fds[1]);
}

/* ═══ sys_spawn 测试 ═══ */

static void dummy_task_entry(void *arg)
{
    (void)arg;
    /* 空任务，仅用于测试 spawn */
}

TEST_F(KernelSyscallTest, SysSpawnCreatesTask)
{
    kern_pid_t pid = sys_spawn("test-syscall", dummy_task_entry, NULL, 0);
    EXPECT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_STREQ(task->name, "test-syscall");
}

TEST_F(KernelSyscallTest, SysSpawnMultipleTasksUniquePids)
{
    kern_pid_t pid1 = sys_spawn("task1", dummy_task_entry, NULL, 0);
    kern_pid_t pid2 = sys_spawn("task2", dummy_task_entry, NULL, 0);

    EXPECT_GE(pid1, 0);
    EXPECT_GE(pid2, 0);
    EXPECT_NE(pid1, pid2);
}

/* ═══ 分发器直接调用测试 ═══ */

TEST_F(KernelSyscallTest, KernSyscallOpen)
{
    long ret = kern_syscall(SYS_OPEN, (long)"/dev/null", (long)KERN_O_WRONLY, 0, 0);
    EXPECT_GE(ret, 0);
    kern_syscall(SYS_CLOSE, ret, 0, 0, 0);
}

TEST_F(KernelSyscallTest, KernSyscallWriteRead)
{
    /* 通过 pipe 测试 read/write syscall */
    kern_fd_t fds[2];
    long pret = kern_syscall(SYS_PIPE, (long)fds, 0, 0, 0);
    ASSERT_EQ(pret, (long)KERN_OK);

    const char *text = "direct syscall test";
    long nw = kern_syscall(SYS_WRITE, (long)fds[1], (long)text,
                           (long)strlen(text), 0);
    EXPECT_EQ(nw, (long)strlen(text));

    char buf[64] = {0};
    long nr = kern_syscall(SYS_READ, (long)fds[0], (long)buf,
                           (long)sizeof(buf), 0);
    EXPECT_EQ(nr, (long)strlen(text));
    EXPECT_STREQ(buf, text);

    kern_syscall(SYS_CLOSE, (long)fds[0], 0, 0, 0);
    kern_syscall(SYS_CLOSE, (long)fds[1], 0, 0, 0);
}

TEST_F(KernelSyscallTest, KernSyscallYieldSleep)
{
    long ret;

    ret = kern_syscall(SYS_YIELD, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);

    ret = kern_syscall(SYS_SLEEP, 1, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

/* ═══ 边界条件 ═══ */

TEST_F(KernelSyscallTest, SysOpenEmptyPath)
{
    kern_fd_t fd = sys_open("", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}

TEST_F(KernelSyscallTest, SysCloseSameFdTwice)
{
    kern_fd_t fd = sys_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    EXPECT_EQ(sys_close(fd), KERN_OK);
    /* 第二次 close 应该失败 */
    EXPECT_LT(sys_close(fd), 0);
}

TEST_F(KernelSyscallTest, MaxFdsForTask)
{
    /* 打开文件描述符直到达到上限 */
    kern_fd_t fds[KERN_MAX_FD_PER_TASK + 1];
    int count = 0;

    for (int i = 0; i < KERN_MAX_FD_PER_TASK + 1; i++) {
        fds[i] = sys_open("/dev/null", KERN_O_WRONLY);
        if (fds[i] >= 0) {
            count++;
        } else {
            break;
        }
    }

    /* 应该无法超过 KERN_MAX_FD_PER_TASK */
    EXPECT_LE(count, KERN_MAX_FD_PER_TASK);

    /* 清理 */
    for (int i = 0; i < count; i++) {
        sys_close(fds[i]);
    }
}
