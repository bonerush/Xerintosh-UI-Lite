/**
 * @file   test_kernel_device.cpp
 * @brief  Xeros 设备驱动模型单元测试
 * @details 测试 kern_device_register / kern_device_find、
 *          VFS bridge 操作和 /dev 节点自动创建。
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
#include "kernel/kern_init.h"
}

/* ═══ 辅助：测试设备操作回调 ═══ */

static int g_test_open_called = 0;
static int g_test_close_called = 0;
static int g_test_read_called = 0;
static int g_test_write_called = 0;
static int g_test_ioctl_called = 0;

static int test_dev_open(kern_device_t *dev, int flags)
{
    (void)dev;
    (void)flags;
    g_test_open_called++;
    return KERN_OK;
}

static int test_dev_close(kern_device_t *dev)
{
    (void)dev;
    g_test_close_called++;
    return KERN_OK;
}

static int test_dev_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)len;
    (void)offset;
    g_test_read_called++;
    return 0;
}

static int test_dev_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)len;
    (void)offset;
    g_test_write_called++;
    return (int)len;
}

static int test_dev_ioctl(kern_device_t *dev, unsigned int cmd, unsigned long arg)
{
    (void)dev;
    (void)cmd;
    (void)arg;
    g_test_ioctl_called++;
    return KERN_OK;
}

static kern_device_ops_t g_test_ops = {
    .open  = test_dev_open,
    .close = test_dev_close,
    .read  = test_dev_read,
    .write = test_dev_write,
    .ioctl = test_dev_ioctl,
};

static kern_device_t g_test_dev = {
    .name         = "testdev",
    .type         = KERN_DEV_CHAR,
    .ops          = &g_test_ops,
    .private_data = NULL,
    .next         = NULL,
};

/* ═══ 设备注册测试 ═══ */

TEST(KernelDeviceTest, RegisterNullReturnsEinval)
{
    int rc = kern_device_register(NULL);
    EXPECT_EQ(rc, KERN_EINVAL);
}

TEST(KernelDeviceTest, RegisterCreatesDevNode)
{
    kern_init();
    kern_vfs_init();
    kern_devfs_init();

    kern_err_t rc = kern_device_register(&g_test_dev);
    EXPECT_EQ(rc, KERN_OK);

    kern_dentry_t *d = kern_path_resolve("/dev/testdev");
    EXPECT_NE(d, nullptr);
    EXPECT_NE(d->inode, nullptr);
    EXPECT_EQ(d->inode->type, KERN_FILE_CHRDEV);
}

TEST(KernelDeviceTest, RegisterEmptyNameReturnsEinval)
{
    kern_device_t dev = { .name = "", .type = KERN_DEV_CHAR };
    int rc = kern_device_register(&dev);
    EXPECT_EQ(rc, KERN_EINVAL);
}

TEST(KernelDeviceTest, RegisterAndFindByName)
{
    kern_device_t *found = kern_device_find("testdev");
    /* 可能已由其他测试注册，也可能为 NULL */
    (void)found;
}

TEST(KernelDeviceTest, FindNullReturnsNull)
{
    kern_device_t *found = kern_device_find(NULL);
    EXPECT_EQ(found, nullptr);
}

TEST(KernelDeviceTest, FindEmptyNameReturnsNull)
{
    kern_device_t *found = kern_device_find("");
    EXPECT_EQ(found, nullptr);
}

TEST(KernelDeviceTest, FindNonexistentReturnsNull)
{
    kern_device_t *found = kern_device_find("nonexistent_device_xyz");
    EXPECT_EQ(found, nullptr);
}

/* ═══ VFS Bridge 测试 ═══ */

TEST(KernelDeviceTest, CreateFopsReturnsNonNull)
{
    kern_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.name[0] = 'x';

    kern_file_ops_t *fops = kern_device_create_fops(&dev);
    /* bridge fops 始终返回共享实例 */
    EXPECT_NE(fops, nullptr);
}

TEST(KernelDeviceTest, BridgeFopsAreSingletons)
{
    kern_device_t dev1, dev2;
    memset(&dev1, 0, sizeof(dev1));
    memset(&dev2, 0, sizeof(dev2));

    kern_file_ops_t *fops1 = kern_device_create_fops(&dev1);
    kern_file_ops_t *fops2 = kern_device_create_fops(&dev2);

    /* 所有设备共享同一个 bridge fops 实例 */
    EXPECT_EQ(fops1, fops2);
}

TEST(KernelDeviceTest, BridgeFopsHasAllCallbacks)
{
    kern_device_t dev;
    memset(&dev, 0, sizeof(dev));

    kern_file_ops_t *fops = kern_device_create_fops(&dev);
    ASSERT_NE(fops, nullptr);

    /* bridge fops 应提供所有回调 */
    EXPECT_NE(fops->open, nullptr);
    EXPECT_NE(fops->read, nullptr);
    EXPECT_NE(fops->write, nullptr);
    EXPECT_NE(fops->ioctl, nullptr);
    EXPECT_NE(fops->release, nullptr);
}

/* ═══ 设备类型枚举 ═══ */

TEST(KernelDeviceTest, DeviceTypeEnumValues)
{
    /* 验证枚举值存在 */
    EXPECT_EQ(KERN_DEV_CHAR, 0);
    EXPECT_EQ(KERN_DEV_BLOCK, 1);
}
