/**
 * @file   dev_fb0.c
 * @brief  /dev/fb0 帧缓冲设备实现
 * @details 将 HAL 显示层绘制原语映射为 VFS 文件操作。
 *          write() 接受二进制命令协议批量执行绘制操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "dev_fb0.h"
#include "hal/hal_display.h"

#include <string.h>

/* ═══ 写命令处理 ═══ */

static ssize_t dev_fb0_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;
    size_t pos = 0;

    while (pos < len) {
        uint8_t cmd = (uint8_t)buf[pos];
        pos++;

        switch (cmd) {
        case DEV_FB_CMD_PIXEL: {
            if (pos + 6 > len) return KERN_EINVAL;
            int16_t x, y;
            uint16_t color;
            memcpy(&x,     buf + pos, 2); pos += 2;
            memcpy(&y,     buf + pos, 2); pos += 2;
            memcpy(&color, buf + pos, 2); pos += 2;
            hal_draw_pixel(x, y, color);
            break;
        }
        case DEV_FB_CMD_FILL_RECT: {
            if (pos + 10 > len) return KERN_EINVAL;
            int16_t x, y, w, h;
            uint16_t color;
            memcpy(&x,     buf + pos, 2); pos += 2;
            memcpy(&y,     buf + pos, 2); pos += 2;
            memcpy(&w,     buf + pos, 2); pos += 2;
            memcpy(&h,     buf + pos, 2); pos += 2;
            memcpy(&color, buf + pos, 2); pos += 2;
            hal_draw_fill_rect(x, y, w, h, color);
            break;
        }
        case DEV_FB_CMD_CLEAR: {
            if (pos + 2 > len) return KERN_EINVAL;
            uint16_t color;
            memcpy(&color, buf + pos, 2); pos += 2;
            (void)color;
            hal_display_clear();
            break;
        }
        case DEV_FB_CMD_FLUSH:
            hal_display_flush();
            break;
        default:
            return KERN_EINVAL;
        }
    }

    return (ssize_t)len;
}

/* ═══ ioctl ═══ */

static int dev_fb0_ioctl(kern_file_t *f, unsigned int cmd, unsigned long arg)
{
    (void)f;

    switch (cmd) {
    case DEV_FB_IOCTL_GET_WIDTH:
        return (int)SCREEN_WIDTH;
    case DEV_FB_IOCTL_GET_HEIGHT:
        return (int)SCREEN_HEIGHT;
    case DEV_FB_IOCTL_SET_ROTATION:
        /* 硬件环境: M5.Display.setRotation(arg) 但 HAL 层未暴露旋转 API，
         * 保留此 ioctl 供未来扩展 */
        (void)arg;
        return KERN_OK;
    default:
        return KERN_ENOTTY;
    }
}

/* ═══ release ═══ */

static int dev_fb0_release(kern_file_t *f)
{
    (void)f;
    return KERN_OK;
}

/* ═══ 操作表 ═══ */

static kern_file_ops_t g_dev_fb0_fops = {
    .read    = NULL,  /* 帧缓冲不支持读取 */
    .write   = dev_fb0_write,
    .ioctl   = dev_fb0_ioctl,
    .release = dev_fb0_release,
};

kern_file_ops_t *dev_fb0_get_fops(void)
{
    return &g_dev_fb0_fops;
}
