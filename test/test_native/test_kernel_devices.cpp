/**
 * @file   test_kernel_devices.cpp
 * @brief  物理设备映射 native 测试
 * @details 测试 /dev/fb0, /dev/input0, /dev/ttyS0 的 VFS 集成。
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
#include "kernel/devices/kern_devices.h"
#include "kernel/kern_init.h"
#include "kernel/devices/dev_fb0.h"
#include "kernel/devices/dev_input0.h"
#include "kernel/devices/dev_ttyS0.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
}

/* ═══ 测试夹具 ═══ */

class KernelDevicesTest : public ::testing::Test {
protected:
    void SetUp() override {
        kern_clear_panic();
        kern_devfs_init();
        kern_log_set_level(KERN_LOG_ERROR);
        hal_display_init();
        hal_input_reset_events();
    }
};

/* ═══ 辅助函数 ═══ */

static void write_fb_cmd(kern_fd_t fd, const uint8_t *cmd, size_t len)
{
    ssize_t n = kern_write(fd, (const char *)cmd, len);
    ASSERT_EQ(n, (ssize_t)len);
}

/* ═══ /dev/fb0 测试 ═══ */

TEST_F(KernelDevicesTest, Fb0OpenSucceeds)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0WritePixelDrawsToFramebuffer)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    /* 先清屏再用 hal_test_fb_read 验证像素 */
    uint8_t cmd[7];
    cmd[0] = FB_CMD_PIXEL;
    int16_t x = 10, y = 20;
    uint16_t color = 0xF800;
    memcpy(cmd + 1, &x, 2);
    memcpy(cmd + 3, &y, 2);
    memcpy(cmd + 5, &color, 2);

    write_fb_cmd(fd, cmd, sizeof(cmd));

    EXPECT_EQ(hal_test_fb_read(10, 20), 0xF800);
    EXPECT_EQ(hal_test_fb_read(0, 0), 0x0000);  /* 未绘制的像素应为黑色 */

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0WriteClearFillsBlack)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    /* 先画一个像素 */
    uint8_t pixel_cmd[7];
    pixel_cmd[0] = FB_CMD_PIXEL;
    int16_t x = 30, y = 40;
    uint16_t color = 0xFFFF;
    memcpy(pixel_cmd + 1, &x, 2);
    memcpy(pixel_cmd + 3, &y, 2);
    memcpy(pixel_cmd + 5, &color, 2);
    write_fb_cmd(fd, pixel_cmd, sizeof(pixel_cmd));
    EXPECT_EQ(hal_test_fb_read(30, 40), 0xFFFF);

    /* 清屏 */
    uint8_t clear_cmd[3];
    clear_cmd[0] = FB_CMD_CLEAR;
    uint16_t bg = 0x0000;
    memcpy(clear_cmd + 1, &bg, 2);
    write_fb_cmd(fd, clear_cmd, sizeof(clear_cmd));
    EXPECT_EQ(hal_test_fb_read(30, 40), 0x0000);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0WriteFlushDoesNotCrash)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    uint8_t flush_cmd = FB_CMD_FLUSH;
    write_fb_cmd(fd, &flush_cmd, 1);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0WriteFillRect)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    uint8_t cmd[11];
    cmd[0] = FB_CMD_FILL_RECT;
    int16_t x = 5, y = 10, w = 20, h = 15;
    uint16_t color = 0x07E0;
    memcpy(cmd + 1, &x, 2);
    memcpy(cmd + 3, &y, 2);
    memcpy(cmd + 5, &w, 2);
    memcpy(cmd + 7, &h, 2);
    memcpy(cmd + 9, &color, 2);
    write_fb_cmd(fd, cmd, sizeof(cmd));

    /* 检查矩形内部像素 */
    EXPECT_EQ(hal_test_fb_read(5, 10), 0x07E0);
    EXPECT_EQ(hal_test_fb_read(24, 24), 0x07E0);
    /* 矩形外部 */
    EXPECT_EQ(hal_test_fb_read(25, 10), 0x0000);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0IoctlGetWidth)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    int w = kern_ioctl(fd, FB_IOCTL_GET_WIDTH, 0);
    EXPECT_EQ(w, SCREEN_WIDTH);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0IoctlGetHeight)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    int h = kern_ioctl(fd, FB_IOCTL_GET_HEIGHT, 0);
    EXPECT_EQ(h, SCREEN_HEIGHT);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0InvalidCommand)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    uint8_t bad_cmd = 0xFF;
    ssize_t written = kern_write(fd, (const char *)&bad_cmd, 1);
    EXPECT_LT(written, 0);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Fb0TruncatedCommand)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    uint8_t bad[4] = { FB_CMD_PIXEL, 0x01, 0x02, 0x03 };
    ssize_t written = kern_write(fd, (const char *)bad, 4);
    EXPECT_LT(written, 0);

    kern_close(fd);
}

/* ═══ /dev/input0 测试 ═══ */

TEST_F(KernelDevicesTest, Input0OpenSucceeds)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST_F(KernelDevicesTest, Input0ReadShortPress)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    hal_test_inject_event(HAL_BTN_A, HAL_EVENT_SHORT_PRESS);

    uint8_t buf[INPUT_EVENT_SIZE];
    ssize_t n = kern_read(fd, (char *)buf, sizeof(buf));
    EXPECT_EQ(n, (ssize_t)INPUT_EVENT_SIZE);

    input_event_t ev;
    memcpy(&ev, buf, INPUT_EVENT_SIZE);
    EXPECT_EQ(ev.button, (uint8_t)HAL_BTN_A);
    EXPECT_EQ(ev.event, (uint8_t)HAL_EVENT_SHORT_PRESS);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Input0ReadLongPress)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    hal_test_inject_event(HAL_BTN_B, HAL_EVENT_LONG_PRESS);

    uint8_t buf[INPUT_EVENT_SIZE];
    ssize_t n = kern_read(fd, (char *)buf, sizeof(buf));
    EXPECT_EQ(n, (ssize_t)INPUT_EVENT_SIZE);

    input_event_t ev;
    memcpy(&ev, buf, INPUT_EVENT_SIZE);
    EXPECT_EQ(ev.button, (uint8_t)HAL_BTN_B);
    EXPECT_EQ(ev.event, (uint8_t)HAL_EVENT_LONG_PRESS);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Input0ReadNoEventReturnsNone)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    /* 不注入事件，应返回 HAL_EVENT_NONE */

    uint8_t buf[INPUT_EVENT_SIZE];
    ssize_t n = kern_read(fd, (char *)buf, sizeof(buf));
    EXPECT_EQ(n, (ssize_t)INPUT_EVENT_SIZE);

    input_event_t ev;
    memcpy(&ev, buf, INPUT_EVENT_SIZE);
    EXPECT_EQ(ev.event, (uint8_t)HAL_EVENT_NONE);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Input0ReadBufferTooSmall)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    char small_buf[3];
    ssize_t n = kern_read(fd, small_buf, sizeof(small_buf));
    EXPECT_LT(n, 0);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Input0IoctlDoubleClick)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    int rc = kern_ioctl(fd, INPUT_IOCTL_SET_DOUBLE_CLICK, 1);
    EXPECT_EQ(rc, KERN_OK);
    EXPECT_TRUE(hal_input_is_double_click_enabled());

    rc = kern_ioctl(fd, INPUT_IOCTL_SET_DOUBLE_CLICK, 0);
    EXPECT_EQ(rc, KERN_OK);
    EXPECT_FALSE(hal_input_is_double_click_enabled());

    kern_close(fd);
}

TEST_F(KernelDevicesTest, Input0IoctlInvalid)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/input0", KERN_O_RDONLY);
    ASSERT_GE(fd, 0);

    int rc = kern_ioctl(fd, 0xFF, 0);
    EXPECT_EQ(rc, KERN_ENOTTY);

    kern_close(fd);
}

/* ═══ /dev/ttyS0 测试 ═══ */

TEST_F(KernelDevicesTest, TtyS0OpenSucceeds)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/ttyS0", KERN_O_RDWR);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST_F(KernelDevicesTest, TtyS0WriteAndReadBack)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/ttyS0", KERN_O_RDWR);
    ASSERT_GE(fd, 0);

    const char *msg = "hello";
    ssize_t written = kern_write(fd, msg, 5);
    EXPECT_EQ(written, 5);

    char buf[32];
    ssize_t n = kern_read(fd, buf, sizeof(buf));
    EXPECT_EQ(n, 5);
    EXPECT_EQ(memcmp(buf, "hello", 5), 0);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, TtyS0ReadEmptyReturnsZero)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/ttyS0", KERN_O_RDWR);
    ASSERT_GE(fd, 0);

    char buf[32];
    ssize_t n = kern_read(fd, buf, sizeof(buf));
    EXPECT_EQ(n, 0);

    kern_close(fd);
}

TEST_F(KernelDevicesTest, TtyS0MultipleWritesAndReads)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/ttyS0", KERN_O_RDWR);
    ASSERT_GE(fd, 0);

    kern_write(fd, "ABC", 3);
    kern_write(fd, "DEF", 3);

    char buf[8];
    ssize_t n1 = kern_read(fd, buf, 3);
    EXPECT_EQ(n1, 3);
    EXPECT_EQ(memcmp(buf, "ABC", 3), 0);

    ssize_t n2 = kern_read(fd, buf, 3);
    EXPECT_EQ(n2, 3);
    EXPECT_EQ(memcmp(buf, "DEF", 3), 0);

    kern_close(fd);
}

/* ═══ 设备注册完整性测试 ═══ */

TEST_F(KernelDevicesTest, AllDevicesRegistered)
{
    int rc = kern_devices_init();
    EXPECT_EQ(rc, KERN_OK);

    EXPECT_NE(kern_path_resolve("/dev/fb0"), (void*)nullptr);
    EXPECT_NE(kern_path_resolve("/dev/input0"), (void*)nullptr);
    EXPECT_NE(kern_path_resolve("/dev/ttyS0"), (void*)nullptr);
}

TEST_F(KernelDevicesTest, WriteOnlyDeviceRejectsRead)
{
    kern_devices_init();
    kern_fd_t fd = kern_open("/dev/fb0", KERN_O_WRONLY);
    ASSERT_GE(fd, 0);

    char buf[16];
    ssize_t n = kern_read(fd, buf, sizeof(buf));
    EXPECT_LT(n, 0);

    kern_close(fd);
}
