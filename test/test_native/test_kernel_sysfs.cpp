/**
 * @file   test_kernel_sysfs.cpp
 * @brief  Xeros sysfs 单元测试
 * @details 测试 /sys 虚拟文件系统的初始化、文件读写、值持久化、
 *          无效输入拒绝、getter/setter 函数。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_sysfs.h"
}

/* ═══ 辅助：读取文件全部内容 ═══ */

static ssize_t sysfs_read_all(kern_fd_t fd, char *buf, size_t buf_size)
{
    memset(buf, 0, buf_size);
    return kern_read(fd, buf, buf_size - 1);
}

/* ═══ 初始化测试 ═══ */

TEST(KernelSysfsTest, InitDoesNotCrash)
{
    kern_vfs_init();
    kern_sysfs_init();
    SUCCEED();
}

TEST(KernelSysfsTest, InitCreatesSysDir)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_dentry_t *sys_dir = kern_path_resolve("/sys");
    ASSERT_NE(sys_dir, nullptr);
    EXPECT_STREQ(sys_dir->name, "sys");
}

TEST(KernelSysfsTest, InitCreatesSysKernelDir)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_dentry_t *kernel_dir = kern_path_resolve("/sys/kernel");
    ASSERT_NE(kernel_dir, nullptr);
    EXPECT_STREQ(kernel_dir->name, "kernel");
}

/* ═══ /sys/brightness 测试 ═══ */

TEST(KernelSysfsTest, BrightnessFileExists)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelSysfsTest, BrightnessReadDefault)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "255");

    kern_close(fd);
}

TEST(KernelSysfsTest, BrightnessWriteAndReadBack)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "128", 3);
    EXPECT_EQ(n, 3);
    kern_close(fd);

    fd = kern_open("/sys/brightness", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "128");

    kern_close(fd);
}

TEST(KernelSysfsTest, BrightnessWriteInvalidTooLarge)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "999", 3);
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST(KernelSysfsTest, BrightnessWriteInvalidNegative)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "-1", 2);
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST(KernelSysfsTest, BrightnessReadAfterRejectedWriteUnchanged)
{
    kern_vfs_init();
    kern_sysfs_init();

    /* 先尝试写入非法值 */
    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    kern_write(fd, "999", 3);
    kern_close(fd);

    /* 读取应仍为默认值 */
    fd = kern_open("/sys/brightness", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "255");

    kern_close(fd);
}

/* ═══ /sys/rotation 测试 ═══ */

TEST(KernelSysfsTest, RotationFileExists)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/rotation", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelSysfsTest, RotationReadWrite)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/rotation", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(kern_write(fd, "1", 1), 1);
    kern_close(fd);

    fd = kern_open("/sys/rotation", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "1");

    kern_close(fd);
}

TEST(KernelSysfsTest, RotationRejectsOutOfRange)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/rotation", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    ssize_t n = kern_write(fd, "4", 1);
    EXPECT_LT(n, 0);
    kern_close(fd);
}

/* ═══ /sys/anim_speed 测试 ═══ */

TEST(KernelSysfsTest, AnimSpeedReadWrite)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/anim_speed", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(kern_write(fd, "50", 2), 2);
    kern_close(fd);

    fd = kern_open("/sys/anim_speed", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "50");

    kern_close(fd);
}

/* ═══ /sys/anim_enabled 测试 ═══ */

TEST(KernelSysfsTest, AnimEnabledReadWriteZero)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/anim_enabled", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(kern_write(fd, "0", 1), 1);
    kern_close(fd);

    fd = kern_open("/sys/anim_enabled", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "0");

    kern_close(fd);
}

TEST(KernelSysfsTest, AnimEnabledReadWriteOne)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/anim_enabled", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(kern_write(fd, "1", 1), 1);
    kern_close(fd);

    fd = kern_open("/sys/anim_enabled", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "1");

    kern_close(fd);
}

TEST(KernelSysfsTest, AnimEnabledRejectsInvalid)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/anim_enabled", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    ssize_t n = kern_write(fd, "2", 1);
    EXPECT_LT(n, 0);
    kern_close(fd);
}

/* ═══ /sys/kernel/log_level 测试 ═══ */

TEST(KernelSysfsTest, LogLevelFileExists)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/kernel/log_level", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelSysfsTest, LogLevelReadWrite)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/kernel/log_level", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(kern_write(fd, "2", 1), 1);
    kern_close(fd);

    fd = kern_open("/sys/kernel/log_level", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "2");

    kern_close(fd);
}

/* ═══ 持久化测试 ═══ */

TEST(KernelSysfsTest, SysfsFilesArePersistentCloseReopen)
{
    kern_vfs_init();
    kern_sysfs_init();

    /* 写入新值 */
    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);
    kern_write(fd, "100", 3);
    kern_close(fd);

    /* 关闭后重新打开，值应保持 */
    fd = kern_open("/sys/brightness", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = sysfs_read_all(fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "100");

    kern_close(fd);
}

/* ═══ Getter/Setter 测试 ═══ */

TEST(KernelSysfsTest, GetterSetterBrightness)
{
    kern_vfs_init();
    kern_sysfs_init();

    EXPECT_EQ(kern_sysfs_get_brightness(), 255);

    kern_sysfs_set_brightness(128);
    EXPECT_EQ(kern_sysfs_get_brightness(), 128);
}

TEST(KernelSysfsTest, GetterSetterRotation)
{
    kern_vfs_init();
    kern_sysfs_init();

    EXPECT_EQ(kern_sysfs_get_rotation(), 0);

    kern_sysfs_set_rotation(2);
    EXPECT_EQ(kern_sysfs_get_rotation(), 2);
}

TEST(KernelSysfsTest, GetterSetterAnimSpeed)
{
    kern_vfs_init();
    kern_sysfs_init();

    EXPECT_EQ(kern_sysfs_get_anim_speed(), 92);

    kern_sysfs_set_anim_speed(50);
    EXPECT_EQ(kern_sysfs_get_anim_speed(), 50);
}

TEST(KernelSysfsTest, GetterSetterAnimEnabled)
{
    kern_vfs_init();
    kern_sysfs_init();

    EXPECT_EQ(kern_sysfs_get_anim_enabled(), 1);

    kern_sysfs_set_anim_enabled(0);
    EXPECT_EQ(kern_sysfs_get_anim_enabled(), 0);

    kern_sysfs_set_anim_enabled(1);
    EXPECT_EQ(kern_sysfs_get_anim_enabled(), 1);
}

TEST(KernelSysfsTest, GetterSetterLogLevel)
{
    kern_vfs_init();
    kern_sysfs_init();

    EXPECT_EQ(kern_sysfs_get_log_level(), 1);

    kern_sysfs_set_log_level(3);
    EXPECT_EQ(kern_sysfs_get_log_level(), 3);
}

TEST(KernelSysfsTest, SetterClampsOutOfRange)
{
    kern_vfs_init();
    kern_sysfs_init();

    /* 先用 setter 设置一个已知值，再用非法值尝试覆盖 */
    kern_sysfs_set_brightness(128);
    EXPECT_EQ(kern_sysfs_get_brightness(), 128);
    kern_sysfs_set_brightness(999);
    EXPECT_EQ(kern_sysfs_get_brightness(), 128);  /* 不变 */

    kern_sysfs_set_rotation(1);
    EXPECT_EQ(kern_sysfs_get_rotation(), 1);
    kern_sysfs_set_rotation(5);
    EXPECT_EQ(kern_sysfs_get_rotation(), 1);  /* 不变 */

    kern_sysfs_set_anim_enabled(0);
    EXPECT_EQ(kern_sysfs_get_anim_enabled(), 0);
    kern_sysfs_set_anim_enabled(2);
    EXPECT_EQ(kern_sysfs_get_anim_enabled(), 0);  /* 不变 */
}

/* ═══ 文件系统层面读/写 edge case ═══ */

TEST(KernelSysfsTest, WriteNonNumericReturnsError)
{
    kern_vfs_init();
    kern_sysfs_init();

    kern_fd_t fd = kern_open("/sys/brightness", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "abc", 3);
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST(KernelSysfsTest, ReadFromInvalidFdReturnsError)
{
    kern_vfs_init();
    kern_sysfs_init();

    char buf[16];
    ssize_t n = kern_read(999, buf, sizeof(buf));
    EXPECT_LT(n, 0);
}
