/**
 * @file   test_kernel_devfs.cpp
 * @brief  Xeros 设备文件系统（devfs）与 /dev/null 单元测试
 * @details 测试设备注册、/dev/null 读写特性、错误处理。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_devfs.h"
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

/* ═══ 设备注册测试 ═══ */

static ssize_t null_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;
    (void)buf;
    (void)len;
    return 0;  /* EOF */
}

static ssize_t null_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;
    (void)buf;
    return (ssize_t)len;  /* 丢弃数据，假装写入成功 */
}

static kern_file_ops_t null_fops = {
    .read = null_read,
    .write = null_write,
    .ioctl = NULL,
    .release = NULL,
};

TEST(KernelDevFSTest, RegisterDevice)
{
    kern_devfs_init();
    int rc = kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);
    EXPECT_EQ(rc, KERN_OK);

    kern_dentry_t *d = kern_path_resolve("/dev/null");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "null");
}

TEST(KernelDevFSTest, RegisterDeviceNullNameReturnsError)
{
    kern_devfs_init();
    int rc = kern_dev_register(NULL, &null_fops, KERN_FILE_CHRDEV, NULL);
    EXPECT_LT(rc, 0);
}

TEST(KernelDevFSTest, RegisterDeviceNullFopsReturnsError)
{
    kern_devfs_init();
    int rc = kern_dev_register("bad", NULL, KERN_FILE_CHRDEV, NULL);
    EXPECT_LT(rc, 0);
}

/* ═══ /dev/null 行为测试 ═══ */

TEST(KernelNullTest, OpenNull)
{
    kern_devfs_init();
    kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);

    kern_fd_t fd = kern_open("/dev/null", KERN_O_RDWR);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelNullTest, WriteToNullReturnsLength)
{
    kern_devfs_init();
    kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);

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
    kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);

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
    kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);

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
    kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);

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
    kern_dev_register("null", &null_fops, KERN_FILE_CHRDEV, NULL);

    kern_fd_t fd = kern_open("/dev/nonexistent", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}
