/**
 * @file   test_kernel_devfs.cpp
 * @brief  Xeros 设备文件系统（devfs）与 /dev/null 单元测试
 * @details 测试设备注册、/dev/null 读写特性、错误处理。
 *          使用统一设备模型 kern_device_t + kern_device_register()。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_devfs.h"
#include "kernel/kern_device.h"
}

/* ═══ devfs 初始化测试 ═══ */

TEST(KernelDevFSTest, DevFsInitCreatesDevDir)
{
    kern_vfs_init();
    kern_devfs_init();

    kern_dentry_t *dev = kern_path_resolve("/dev");
    ASSERT_NE(dev, nullptr);
    EXPECT_STREQ(dev->name, "dev");
}

TEST(KernelDevFSTest, DevFsInitBeforeVfsIsSafe)
{
    /* devfs_init 应该调用 vfs_init */
    kern_devfs_init();

    kern_dentry_t *root = kern_vfs_get_root();
    ASSERT_NE(root, nullptr);
}

/* ═══ /dev/null 设备 ═══ */

static kern_err_t null_open(kern_device_t *dev, int flags)
{
    (void)dev;
    (void)flags;
    return KERN_OK;
}

static kern_err_t null_close(kern_device_t *dev)
{
    (void)dev;
    return KERN_OK;
}

static kern_err_t null_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)len;
    (void)offset;
    return 0;  /* EOF */
}

static kern_err_t null_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)offset;
    return (kern_err_t)len;  /* 丢弃数据，假装写入成功 */
}

static kern_device_ops_t g_null_ops = {
    .open  = null_open,
    .close = null_close,
    .read  = null_read,
    .write = null_write,
    .ioctl = NULL,
};

static kern_device_t g_null_dev = {
    .name         = "null",
    .type         = KERN_DEV_CHAR,
    .ops          = &g_null_ops,
    .private_data = NULL,
    .next         = NULL,
};

/* ═══ 设备注册测试 ═══ */

TEST(KernelDevFSTest, RegisterDevice)
{
    kern_devfs_init();
    kern_err_t rc = kern_device_register(&g_null_dev);
    EXPECT_EQ(rc, KERN_OK);

    kern_dentry_t *d = kern_path_resolve("/dev/null");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "null");
}

TEST(KernelDevFSTest, RegisterDeviceNullDevReturnsError)
{
    kern_devfs_init();
    kern_err_t rc = kern_device_register(NULL);
    EXPECT_LT(rc, 0);
}

TEST(KernelDevFSTest, RegisterDeviceEmptyNameReturnsError)
{
    kern_devfs_init();

    kern_device_t bad = {
        .name = "",
        .type = KERN_DEV_CHAR,
        .ops  = &g_null_ops,
    };
    kern_err_t rc = kern_device_register(&bad);
    EXPECT_LT(rc, 0);
}

/* ═══ /dev/null 行为测试 ═══ */

TEST(KernelNullTest, OpenNull)
{
    kern_devfs_init();
    kern_device_register(&g_null_dev);

    kern_fd_t fd = kern_open("/dev/null", KERN_O_RDWR);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelNullTest, WriteToNullReturnsLength)
{
    kern_devfs_init();
    kern_device_register(&g_null_dev);

    kern_fd_t fd = kern_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    char data[128];
    memset(data, 'A', sizeof(data));
    ssize_t n = kern_write(fd, data, sizeof(data));
    EXPECT_EQ(n, (ssize_t)sizeof(data));

    kern_close(fd);
}

TEST(KernelNullTest, ReadFromNullReturnsEOF)
{
    kern_devfs_init();
    kern_device_register(&g_null_dev);

    kern_fd_t fd = kern_open("/dev/null", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = kern_read(fd, buf, sizeof(buf));
    EXPECT_EQ(n, 0);  /* /dev/null always returns EOF */

    kern_close(fd);
}

TEST(KernelNullTest, WriteMultipleTimesToNull)
{
    kern_devfs_init();
    kern_device_register(&g_null_dev);

    kern_fd_t fd = kern_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    /* 多次写入不应该崩溃 */
    EXPECT_EQ(kern_write(fd, "hello", 5), 5);
    EXPECT_EQ(kern_write(fd, "world", 5), 5);
    EXPECT_EQ(kern_write(fd, "", 0), 0);
    EXPECT_EQ(kern_write(fd, "x", 1), 1);

    kern_close(fd);
}

TEST(KernelNullTest, WriteVeryLargeToNull)
{
    kern_devfs_init();
    kern_device_register(&g_null_dev);

    kern_fd_t fd = kern_open("/dev/null", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    /* 大块数据写入不应该崩溃 */
    char big[2048];
    memset(big, 'X', sizeof(big));
    ssize_t n = kern_write(fd, big, sizeof(big));
    EXPECT_EQ(n, (ssize_t)sizeof(big));

    kern_close(fd);
}

TEST(KernelNullTest, CannotOpenNonexistentDevice)
{
    kern_devfs_init();
    kern_device_register(&g_null_dev);

    kern_fd_t fd = kern_open("/dev/nonexistent", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}
