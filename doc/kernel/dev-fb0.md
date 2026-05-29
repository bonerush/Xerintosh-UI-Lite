# /dev/fb0 帧缓冲设备

> **Parent:** [内核子系统总览](index.md) | **Related:** [物理设备驱动](kern-devices.md), [显示驱动](../hal/display.md)

## 概述

`/dev/fb0` 是 Xeros 内核的**帧缓冲设备**，将 HAL 显示层的绘制原语映射为 VFS 文件操作。用户程序（或后续内核模块）可以通过 `write()` 向 `/dev/fb0` 写入二进制命令，批量执行绘制操作，而无需直接调用 HAL API。

---

## 命令协议

*📄 Source: [dev_fb0.c](../../src/kernel/devices/dev_fb0.c#L17-L66)*

`write()` 接受二进制命令流，每条命令以 **1 字节操作码**开头，后接变长参数：

| 命令 | 操作码 | 参数 | 说明 |
|------|--------|------|------|
| `DEV_FB_CMD_PIXEL` | `0x01` | `int16_t x, int16_t y, uint16_t color` | 绘制单个像素 |
| `DEV_FB_CMD_FILL_RECT` | `0x02` | `int16_t x, y, w, h, uint16_t color` | 绘制填充矩形 |
| `DEV_FB_CMD_CLEAR` | `0x03` | `uint16_t color` | 清屏（当前忽略 color，调用 `hal_display_clear()`） |
| `DEV_FB_CMD_FLUSH` | `0x04` | 无 | 刷新缓冲区到屏幕 |

### 命令流示例

```c
/* 在屏幕上画一个红色矩形 */
char buf[32];
int pos = 0;

buf[pos++] = DEV_FB_CMD_CLEAR;
buf[pos++] = 0x00; buf[pos++] = 0x00;  /* color = black */

buf[pos++] = DEV_FB_CMD_FILL_RECT;
memcpy(buf + pos, &(int16_t){10}, 2); pos += 2;   /* x */
memcpy(buf + pos, &(int16_t){20}, 2); pos += 2;   /* y */
memcpy(buf + pos, &(int16_t){30}, 2); pos += 2;   /* w */
memcpy(buf + pos, &(int16_t){40}, 2); pos += 2;   /* h */
memcpy(buf + pos, &(uint16_t){0xF800}, 2); pos += 2; /* color = red */

buf[pos++] = DEV_FB_CMD_FLUSH;

kern_write(fd, buf, pos);
```

---

### 中文伪代码拆解

```
函数 帧缓冲写入(文件, 缓冲区, 长度) {
    位置 = 0

    while (位置 < 长度) {
        命令 = 缓冲区[位置]
        位置++

        switch (命令) {
            case 绘制像素:
                if (剩余长度不足6字节) return 参数错误
                读取 x(2字节), y(2字节), color(2字节)
                HAL绘制像素(x, y, color)
                break

            case 填充矩形:
                if (剩余长度不足10字节) return 参数错误
                读取 x, y, w, h, color
                HAL填充矩形(x, y, w, h, color)
                break

            case 清屏:
                if (剩余长度不足2字节) return 参数错误
                读取 color（当前忽略）
                HAL清屏()
                break

            case 刷新:
                HAL刷新到屏幕()
                break

            default:
                return 参数错误
        }
    }

    return 已处理字节数
}
```

**核心思想**：通过紧凑的二进制协议，将多个绘制操作打包为一次 `write()` 调用，减少系统调用开销。所有参数使用小端序 `int16_t` / `uint16_t`。

---

## ioctl 接口

*📄 Source: [dev_fb0.c](../../src/kernel/devices/dev_fb0.c#L70-L87)*

```c
static int dev_fb0_ioctl(kern_file_t *f, unsigned int cmd, unsigned long arg)
{
    (void)f;

    switch (cmd) {
    case DEV_FB_IOCTL_GET_WIDTH:
        return (int)SCREEN_WIDTH;
    case DEV_FB_IOCTL_GET_HEIGHT:
        return (int)SCREEN_HEIGHT;
    case DEV_FB_IOCTL_SET_ROTATION:
        (void)arg;
        return KERN_OK;   /* 保留接口，当前未实现 */
    default:
        return KERN_ENOTTY;
    }
}
```

| ioctl | 参数 | 返回值 |
|-------|------|--------|
| `DEV_FB_IOCTL_GET_WIDTH` | — | 屏幕宽度（80 或 160） |
| `DEV_FB_IOCTL_GET_HEIGHT` | — | 屏幕高度（160 或 80） |
| `DEV_FB_IOCTL_SET_ROTATION` | 旋转角度 | `KERN_OK`（保留接口） |

---

## 文件操作表

*📄 Source: [dev_fb0.c](../../src/kernel/devices/dev_fb0.c#L99-L109)*

```c
static kern_file_ops_t g_dev_fb0_fops = {
    .read    = NULL,  /* 帧缓冲不支持读取 */
    .write   = dev_fb0_write,
    .ioctl   = dev_fb0_ioctl,
    .release = dev_fb0_release,
};
```

**注意**：`/dev/fb0` 不支持 `read()`。如果用户需要读取屏幕像素，应通过其他机制（如截图到内存缓冲区）实现。

---

## 当前使用状态

当前 UI 渲染任务（`ui_task_main`）**仍直接调用 HAL API**，不经过 `/dev/fb0`。帧缓冲设备是为未来扩展准备的：

1. **用户态绘图**：允许 Shell 命令或第三方 App 通过 `write(/dev/fb0, ...)` 绘制图形
2. **远程绘图**：通过串口协议将绘图命令转发到 `/dev/fb0`
3. **录屏/截图**：未来可通过扩展 `read()` 实现屏幕读取

---

> **See Also:** [物理设备驱动](kern-devices.md) | [显示驱动](../hal/display.md) | [VFS 核心](kern-vfs.md)
