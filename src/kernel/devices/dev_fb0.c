/**
 * @file   dev_fb0.c
 * @brief  /dev/fb0 帧缓冲设备实现（统一设备模型）
 * @details 将 HAL 显示层绘制原语映射为 kern_device_ops_t 回调。
 *          write() 接受二进制命令协议批量执行绘制操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "dev_fb0.h"
#include "hal/hal_display.h"

#include <string.h>

/* ═══ 设备回调 ═══ */

static kern_err_t dev_fb0_open(kern_device_t *dev, int flags)
{
    (void)dev;
    (void)flags;
    return KERN_OK;
}

static kern_err_t dev_fb0_close(kern_device_t *dev)
{
    (void)dev;
    return KERN_OK;
}

static kern_err_t dev_fb0_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)buf;
    (void)len;
    (void)offset;
    return KERN_EINVAL;  /* 帧缓冲不支持读取 */
}

static kern_err_t dev_fb0_write(kern_device_t *dev, const void *buf, size_t len, size_t *offset)
{
    (void)dev;
    (void)offset;

    if (buf == NULL) return KERN_EINVAL;
    const uint8_t *p = (const uint8_t *)buf;
    size_t pos = 0;

    while (pos < len) {
        uint8_t cmd = p[pos++];

        switch (cmd) {
        case DEV_FB_CMD_PIXEL: {
            if (pos + 6 > len) return KERN_EINVAL;
            int16_t x, y;
            uint16_t color;
            memcpy(&x,     p + pos, 2); pos += 2;
            memcpy(&y,     p + pos, 2); pos += 2;
            memcpy(&color, p + pos, 2); pos += 2;
            hal_draw_pixel(x, y, color);
            break;
        }
        case DEV_FB_CMD_FILL_RECT: {
            if (pos + 10 > len) return KERN_EINVAL;
            int16_t x, y, w, h;
            uint16_t color;
            memcpy(&x,     p + pos, 2); pos += 2;
            memcpy(&y,     p + pos, 2); pos += 2;
            memcpy(&w,     p + pos, 2); pos += 2;
            memcpy(&h,     p + pos, 2); pos += 2;
            memcpy(&color, p + pos, 2); pos += 2;
            hal_draw_fill_rect(x, y, w, h, color);
            break;
        }
        case DEV_FB_CMD_CLEAR: {
            if (pos + 2 > len) return KERN_EINVAL;
            uint16_t color;
            memcpy(&color, p + pos, 2); pos += 2;
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

    return (kern_err_t)len;
}

static kern_err_t dev_fb0_ioctl(kern_device_t *dev, unsigned int cmd, unsigned long arg)
{
    (void)dev;
    (void)arg;

    switch (cmd) {
    case DEV_FB_IOCTL_GET_WIDTH:
        return (kern_err_t)SCREEN_WIDTH;
    case DEV_FB_IOCTL_GET_HEIGHT:
        return (kern_err_t)SCREEN_HEIGHT;
    case DEV_FB_IOCTL_SET_ROTATION:
        hal_display_set_rotation((int)arg);
        return KERN_OK;
    default:
        return KERN_ENOTTY;
    }
}

/* ═══ 设备操作表 ═══ */

static kern_device_ops_t g_fb0_ops = {
    .open  = dev_fb0_open,
    .close = dev_fb0_close,
    .read  = dev_fb0_read,
    .write = dev_fb0_write,
    .ioctl = dev_fb0_ioctl,
};

/* ═══ 设备描述符 ═══ */

kern_device_t g_fb0_dev = {
    .name         = "fb0",
    .type         = KERN_DEV_CHAR,
    .ops          = &g_fb0_ops,
    .private_data = NULL,
    .next         = NULL,
};
