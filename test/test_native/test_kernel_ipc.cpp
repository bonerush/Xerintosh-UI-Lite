/**
 * @file   test_kernel_ipc.cpp
 * @brief  IPC（pipe + message queue）native 测试
 * @details 测试匿名管道和命名消息队列的完整功能。
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

/* ═══ Message Queue 测试 ═══ */

TEST_F(KernelIPCTest, MQOpenCreatesNewQueue)
{
    kern_fd_t fd = kern_mq_open("testq");
    EXPECT_GE(fd, 0);
    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQOpenSameNameReopens)
{
    kern_fd_t fd1 = kern_mq_open("shared");
    ASSERT_GE(fd1, 0);

    kern_fd_t fd2 = kern_mq_open("shared");
    EXPECT_GE(fd2, 0);
    /* 同一队列的不同 fd */
    EXPECT_NE(fd1, fd2);

    kern_mq_close(fd1);
    kern_mq_close(fd2);
}

TEST_F(KernelIPCTest, MQSendAndRecv)
{
    kern_fd_t fd = kern_mq_open("testq2");
    ASSERT_GE(fd, 0);

    const char *msg = "hello mq";
    int rc = kern_mq_send(fd, 1, msg, 8);
    EXPECT_EQ(rc, KERN_OK);

    char buf[128];
    ssize_t n = kern_mq_recv(fd, 1, buf, sizeof(buf));
    EXPECT_EQ(n, 8);
    EXPECT_EQ(memcmp(buf, "hello mq", 8), 0);

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQRecvFiltersByType)
{
    kern_fd_t fd = kern_mq_open("typedq");
    ASSERT_GE(fd, 0);

    kern_mq_send(fd, 1, "type1", 6);
    kern_mq_send(fd, 2, "type2", 6);

    /* 接收类型 2 */
    char buf[128];
    ssize_t n = kern_mq_recv(fd, 2, buf, sizeof(buf));
    EXPECT_EQ(n, 6);
    EXPECT_EQ(memcmp(buf, "type2", 6), 0);

    /* 类型 1 还在队列中 */
    n = kern_mq_recv(fd, 1, buf, sizeof(buf));
    EXPECT_EQ(n, 6);
    EXPECT_EQ(memcmp(buf, "type1", 6), 0);

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQRecvWildcard)
{
    kern_fd_t fd = kern_mq_open("wildq");
    ASSERT_GE(fd, 0);

    kern_mq_send(fd, 5, "first", 6);
    kern_mq_send(fd, 3, "second", 7);

    /* 通配符 0xFF 匹配所有类型（FIFO 顺序） */
    char buf[128];
    ssize_t n = kern_mq_recv(fd, 0xFF, buf, sizeof(buf));
    EXPECT_EQ(n, 6);
    EXPECT_EQ(memcmp(buf, "first", 6), 0);

    n = kern_mq_recv(fd, 0xFF, buf, sizeof(buf));
    EXPECT_EQ(n, 7);
    EXPECT_EQ(memcmp(buf, "second", 7), 0);

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQRecvEmptyReturnsZero)
{
    kern_fd_t fd = kern_mq_open("emptyq");
    ASSERT_GE(fd, 0);

    char buf[128];
    ssize_t n = kern_mq_recv(fd, 0xFF, buf, sizeof(buf));
    EXPECT_EQ(n, 0);

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQRecvNoMatchReturnsZero)
{
    kern_fd_t fd = kern_mq_open("nomatchq");
    ASSERT_GE(fd, 0);

    kern_mq_send(fd, 1, "only type 1", 12);

    char buf[128];
    ssize_t n = kern_mq_recv(fd, 2, buf, sizeof(buf));
    EXPECT_EQ(n, 0); /* 没有类型 2 的消息 */

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQSendMessageTooLarge)
{
    kern_fd_t fd = kern_mq_open("largeq");
    ASSERT_GE(fd, 0);

    char big[200];
    memset(big, 'X', sizeof(big));
    int rc = kern_mq_send(fd, 1, big, sizeof(big));
    EXPECT_EQ(rc, KERN_EINVAL);

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQRecvBufferTooSmall)
{
    kern_fd_t fd = kern_mq_open("smallbufq");
    ASSERT_GE(fd, 0);

    kern_mq_send(fd, 1, "1234567890", 11);

    char buf[3];
    ssize_t n = kern_mq_recv(fd, 1, buf, sizeof(buf));
    /* 缓冲区太小，返回实际消息长度但只拷贝 buf 大小 */
    EXPECT_EQ(n, 11);

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQFIFOOrdering)
{
    kern_fd_t fd = kern_mq_open("fifoq");
    ASSERT_GE(fd, 0);

    kern_mq_send(fd, 1, "A", 2);
    kern_mq_send(fd, 1, "B", 2);
    kern_mq_send(fd, 1, "C", 2);

    char buf[8];
    ssize_t n;

    n = kern_mq_recv(fd, 1, buf, sizeof(buf));
    EXPECT_EQ(n, 2);
    EXPECT_EQ(buf[0], 'A');

    n = kern_mq_recv(fd, 1, buf, sizeof(buf));
    EXPECT_EQ(n, 2);
    EXPECT_EQ(buf[0], 'B');

    n = kern_mq_recv(fd, 1, buf, sizeof(buf));
    EXPECT_EQ(n, 2);
    EXPECT_EQ(buf[0], 'C');

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQCloseAndReopen)
{
    kern_fd_t fd1 = kern_mq_open("reopenq");
    ASSERT_GE(fd1, 0);
    kern_mq_send(fd1, 1, "msg", 4);
    kern_mq_close(fd1);

    /* 重新打开同一队列，消息应还在 */
    kern_fd_t fd2 = kern_mq_open("reopenq");
    ASSERT_GE(fd2, 0);

    char buf[8];
    ssize_t n = kern_mq_recv(fd2, 1, buf, sizeof(buf));
    EXPECT_EQ(n, 4);
    EXPECT_EQ(memcmp(buf, "msg", 4), 0);

    kern_mq_close(fd2);
}

TEST_F(KernelIPCTest, MQExhaustion)
{
    /* 创建超过最大队列数 */
    kern_fd_t fds[KERN_MQ_MAX_QUEUES + 2];
    int count = 0;

    for (int i = 0; i < KERN_MQ_MAX_QUEUES + 2; i++) {
        char name[16];
        snprintf(name, sizeof(name), "exhaust%d", i);
        kern_fd_t fd = kern_mq_open(name);
        if (fd >= 0) {
            fds[count++] = fd;
        } else {
            EXPECT_EQ(fd, KERN_ENOMEM);
            break;
        }
    }

    EXPECT_LE(count, KERN_MQ_MAX_QUEUES);

    for (int i = 0; i < count; i++) {
        kern_mq_close(fds[i]);
    }
}

TEST_F(KernelIPCTest, MQMessageExhaustion)
{
    kern_fd_t fd = kern_mq_open("fulldq");
    ASSERT_GE(fd, 0);

    /* 填满队列 */
    int count = 0;
    for (int i = 0; i < KERN_MQ_MAX_MSGS + 2; i++) {
        int rc = kern_mq_send(fd, (uint8_t)i, "x", 2);
        if (rc == KERN_OK) {
            count++;
        } else {
            EXPECT_EQ(rc, KERN_ENOSPC);
            break;
        }
    }

    EXPECT_LE(count, KERN_MQ_MAX_MSGS);

    /* 清空队列 */
    char buf[8];
    for (int i = 0; i < count; i++) {
        kern_mq_recv(fd, 0xFF, buf, sizeof(buf));
    }

    kern_mq_close(fd);
}

TEST_F(KernelIPCTest, MQInvalidFdReturnsError)
{
    char buf[16];
    EXPECT_EQ(kern_mq_send(999, 1, "x", 2), KERN_EBADF);
    EXPECT_EQ(kern_mq_recv(999, 1, buf, sizeof(buf)), KERN_EBADF);
}
