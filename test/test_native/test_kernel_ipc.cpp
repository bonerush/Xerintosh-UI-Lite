/**
 * @file   test_kernel_ipc.cpp
 * @brief  IPC（pipe）native 测试
 * @details 测试匿名管道的完整功能。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_devfs.h"
#include "kernel/kern_init.h"
#include "kernel/kern_ipc.h"
}

/* ═══ 测试夹具 ═══ */

class KernelIPCTest : public ::testing::Test {
protected:
    void SetUp() override {
        kern_clear_panic();
        kern_vfs_init();
        kern_devfs_init();
        kern_log_set_level(KERN_LOG_ERROR);
    }
};

/* ═══ Pipe 测试 ═══ */

TEST_F(KernelIPCTest, PipeCreateReturnsOK)
{
    kern_fd_t fds[2];
    int rc = kern_pipe(fds);
    EXPECT_EQ(rc, KERN_OK);
    EXPECT_GE(fds[0], 0);
    EXPECT_GE(fds[1], 0);
    EXPECT_NE(fds[0], fds[1]);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeWriteAndReadBack)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    const char *msg = "hello";
    ssize_t written = kern_write(fds[1], msg, 5);
    EXPECT_EQ(written, 5);

    char buf[32];
    ssize_t n = kern_read(fds[0], buf, sizeof(buf));
    EXPECT_EQ(n, 5);
    EXPECT_EQ(memcmp(buf, "hello", 5), 0);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeEmptyReadReturnsZero)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    char buf[32];
    ssize_t n = kern_read(fds[0], buf, sizeof(buf));
    EXPECT_EQ(n, 0);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeFifoOrdering)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    kern_write(fds[1], "ABC", 3);
    kern_write(fds[1], "DEF", 3);

    char buf[8];
    ssize_t n1 = kern_read(fds[0], buf, 3);
    EXPECT_EQ(n1, 3);
    EXPECT_EQ(memcmp(buf, "ABC", 3), 0);

    ssize_t n2 = kern_read(fds[0], buf, 3);
    EXPECT_EQ(n2, 3);
    EXPECT_EQ(memcmp(buf, "DEF", 3), 0);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeReadLessThanWritten)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    kern_write(fds[1], "ABCDEF", 6);

    char buf[4];
    ssize_t n = kern_read(fds[0], buf, sizeof(buf));
    EXPECT_EQ(n, 4);
    EXPECT_EQ(memcmp(buf, "ABCD", 4), 0);

    /* 剩余 2 字节 */
    n = kern_read(fds[0], buf, 2);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(memcmp(buf, "EF", 2), 0);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeBufferWrapAround)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    /* 填充缓冲区到接近满 */
    char data[256];
    memset(data, 'X', 250);
    ssize_t written = kern_write(fds[1], data, 250);
    ASSERT_EQ(written, 250);

    /* 读取 200 字节，腾出空间 */
    char buf[256];
    ssize_t n = kern_read(fds[0], buf, 200);
    ASSERT_EQ(n, 200);

    /* 写入更多数据，测试回绕 */
    memset(data, 'Y', 100);
    written = kern_write(fds[1], data, 100);
    ASSERT_EQ(written, 100);

    /* 读取剩余：50 (旧 X) + 100 (新 Y) */
    char result[256];
    n = kern_read(fds[0], result, sizeof(result));
    EXPECT_EQ(n, 150);
    /* 前 50 字节应是 X */
    EXPECT_EQ(result[0], 'X');
    EXPECT_EQ(result[49], 'X');
    /* 后 100 字节应是 Y */
    EXPECT_EQ(result[50], 'Y');
    EXPECT_EQ(result[149], 'Y');

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeWriteExceedsBuffer)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    /* 写入超过 256B 缓冲区 */
    char data[300];
    memset(data, 'Z', sizeof(data));
    ssize_t written = kern_write(fds[1], data, sizeof(data));
    /* 应该只写入 256B（缓冲区大小） */
    EXPECT_EQ(written, 256);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeCloseReadEnd)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    kern_close(fds[0]); /* 关闭读端 */

    /* 读端关闭后写入应返回 EPIPE（POSIX 行为） */
    char data[16];
    memset(data, 'A', sizeof(data));
    ssize_t written = kern_write(fds[1], data, sizeof(data));
    EXPECT_EQ(written, KERN_EPIPE);

    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeReadFromWriteEndFails)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    char buf[16];
    ssize_t n = kern_read(fds[1], buf, sizeof(buf));
    EXPECT_LT(n, 0);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeWriteToReadEndFails)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    ssize_t n = kern_write(fds[0], "test", 4);
    EXPECT_LT(n, 0);

    kern_close(fds[0]);
    kern_close(fds[1]);
}

TEST_F(KernelIPCTest, PipeCloseBothEndsThenReadReturnsError)
{
    kern_fd_t fds[2];
    ASSERT_EQ(kern_pipe(fds), KERN_OK);

    kern_close(fds[0]);
    kern_close(fds[1]);

    /* 两端都关闭后读写应返回错误 */
    char buf[16];
    ssize_t n = kern_read(fds[0], buf, sizeof(buf));
    EXPECT_LT(n, 0);
}

TEST_F(KernelIPCTest, PipeMultipleCreateAndUse)
{
    /* 创建两对 pipe */
    kern_fd_t a[2], b[2];
    ASSERT_EQ(kern_pipe(a), KERN_OK);
    ASSERT_EQ(kern_pipe(b), KERN_OK);

    kern_write(a[1], "PIPE-A", 6);
    kern_write(b[1], "PIPE-B", 6);

    char buf[16];
    ssize_t n;

    n = kern_read(a[0], buf, 6);
    EXPECT_EQ(n, 6);
    EXPECT_EQ(memcmp(buf, "PIPE-A", 6), 0);

    n = kern_read(b[0], buf, 6);
    EXPECT_EQ(n, 6);
    EXPECT_EQ(memcmp(buf, "PIPE-B", 6), 0);

    kern_close(a[0]); kern_close(a[1]);
    kern_close(b[0]); kern_close(b[1]);
}

TEST_F(KernelIPCTest, PipeExhaustion)
{
    /* 创建超过系统容量的 pipe（fd 或 pipe 槽位之一先耗尽） */
    kern_fd_t fds[KERN_PIPE_MAX * 2 + 2][2];
    int count = 0;
    int exhaust_error = KERN_OK;

    for (int i = 0; i < KERN_PIPE_MAX + 2; i++) {
        int rc = kern_pipe(fds[i]);
        if (rc == KERN_OK) {
            count++;
        } else {
            /* fd 表或 pipe 槽位先耗尽 */
            EXPECT_TRUE(rc == KERN_ENOMEM || rc == KERN_EMFILE);
            exhaust_error = rc;
            break;
        }
    }

    EXPECT_TRUE(exhaust_error != KERN_OK);
    EXPECT_LE(count, KERN_PIPE_MAX);

    /* 清理 */
    for (int i = 0; i < count; i++) {
        kern_close(fds[i][0]);
        kern_close(fds[i][1]);
    }
}
