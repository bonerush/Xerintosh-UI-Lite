/**
 * @file   test_kernel_gpiofs.cpp
 * @brief  /sys/gpio 虚拟文件系统 native 测试
 * @details 覆盖 GPIOFS 初始化、list/pin 读取、输出写入、越界写入、
 *          重复初始化幂等性。
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
#include "kernel/kern_gpiofs.h"
#include "kernel/kern_init.h"
}

/* ═══ 测试夹具 ═══ */

class KernelGpiofsTest : public ::testing::Test {
protected:
    void SetUp() override {
        kern_clear_panic();
        kern_vfs_init();
        kern_devfs_init();
        kern_log_set_level(KERN_LOG_ERROR);
    }
};

/* ═══ 初始化测试 ═══ */

TEST_F(KernelGpiofsTest, InitSucceeds)
{
    kern_err_t rc = kern_gpiofs_init();
    EXPECT_EQ(rc, KERN_OK);
}

TEST_F(KernelGpiofsTest, InitIdempotent)
{
    EXPECT_EQ(kern_gpiofs_init(), KERN_OK);
    EXPECT_EQ(kern_gpiofs_init(), KERN_OK);
}

/* ═══ /sys/gpio/list 测试 ═══ */

TEST_F(KernelGpiofsTest, ListFileExistsAndHasHeader)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/list", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[256];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "PIN"), nullptr);
    EXPECT_NE(strstr(buf, "DIR"), nullptr);
    EXPECT_NE(strstr(buf, "VAL"), nullptr);
    EXPECT_NE(strstr(buf, "FUNC"), nullptr);

    kern_close(fd);
}

/* ═══ 单个引脚文件测试 ═══ */

TEST_F(KernelGpiofsTest, PinFileExistsAndReadable)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/25", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[128];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "pin="), nullptr);
    EXPECT_NE(strstr(buf, "direction="), nullptr);
    EXPECT_NE(strstr(buf, "value="), nullptr);

    kern_close(fd);
}

TEST_F(KernelGpiofsTest, OutputPinWritable)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/25", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    EXPECT_EQ(kern_write(fd, "1", 1), 1);
    EXPECT_EQ(kern_write(fd, "0", 1), 1);

    kern_close(fd);
}

TEST_F(KernelGpiofsTest, InputPinWriteDenied)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    /* 36 号引脚是输入-only（Button A） */
    kern_fd_t fd = kern_open("/sys/gpio/36", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "1", 1);
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST_F(KernelGpiofsTest, InvalidValueWriteDenied)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/25", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    EXPECT_LT(kern_write(fd, "2", 1), 0);
    EXPECT_LT(kern_write(fd, "x", 1), 0);

    kern_close(fd);
}

TEST_F(KernelGpiofsTest, ListFileWriteDenied)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/list", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "1", 1);
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST_F(KernelGpiofsTest, InvalidPinFileDoesNotExist)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/99", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}

/* ═══ 背光引脚仅通过 HAL 控制（K21）═══ */

TEST_F(KernelGpiofsTest, BacklightPinIsReadOnly)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    /* GPIO26 是 LCD 背光，应由 HAL /sys/brightness 控制，gpiofs 不可写 */
    kern_fd_t fd = kern_open("/sys/gpio/26", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    ssize_t n = kern_write(fd, "1", 1);
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST_F(KernelGpiofsTest, BacklightPinMarkedHalOnly)
{
    ASSERT_EQ(kern_gpiofs_init(), KERN_OK);

    kern_fd_t fd = kern_open("/sys/gpio/26", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char buf[128];
    memset(buf, 0, sizeof(buf));
    ssize_t n = kern_read(fd, buf, sizeof(buf) - 1);
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "HAL only"), nullptr);

    kern_close(fd);
}
