# 物理设备驱动（Kern Devices）

> **Parent:** [内核总览](index.md) | **Related:** [devfs](kern-devfs.md), [VFS](kern-vfs.md), [设备驱动模型](kern-device-model.md), [Shell](kern-shell.md)

## 概述

`kern_devices` 模块负责将物理硬件（传感器、显示屏、按键、串口）注册为 `/dev/*` 下的虚拟文件节点。每个设备实现 `kern_file_ops_t` 函数表，使得用户态任务可以通过 `kern_read`、`kern_write`、`kern_ioctl` 访问硬件。

> **v2 注意**：v2 引入了 [统一设备驱动模型](kern-device-model.md)（`kern_device_ops_t` + VFS bridge），`/dev/pwrkey` 已率先迁移。fb0 / input0 / ttyS0 当前仍使用 `kern_file_ops_t` 直接注册，后续版本将统一迁移。

设备统一在 `kern_devices_init()` 中注册，初始化顺序：fb0、input0、ttyS0。

---

## 关键概念

### 设备注册中心

*📄 Source: [devices/kern_devices.c](../../src/kernel/devices/kern_devices.c#L19-L53)*

```c
int kern_devices_init(void)
{
    int rc;

    rc = kern_dev_register("fb0", dev_fb0_get_fops(),
                            KERN_FILE_CHRDEV, NULL);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/fb0: %d", rc);
        return rc;
    }

    rc = kern_dev_register("input0", dev_input0_get_fops(),
                            KERN_FILE_CHRDEV, NULL);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/input0: %d", rc);
        return rc;
    }

    rc = kern_dev_register("ttyS0", dev_ttyS0_get_fops(),
                            KERN_FILE_CHRDEV, NULL);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/ttyS0: %d", rc);
        return rc;
    }

    /* /dev/pwrkey — Proof-of-Concept: 使用统一设备模型 */
    rc = kern_devfs_register_device(&g_pwrkey_dev);
    if (rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "failed to register /dev/pwrkey: %d", rc);
        return rc;
    }

    kern_log(KERN_LOG_INFO, "physical devices registered");
    return KERN_OK;
}
```

`kern_devices_init()` 通过旧版 `kern_dev_register()` 注册 fb0、input0、ttyS0，通过新版 `kern_devfs_register_device()` 注册 pwrkey。所有注册最终都调用 `kern_dentry_register()` 挂入 VFS 树。

### /dev/fb0 — 帧缓冲设备

*📄 Source: [devices/dev_fb0.c](../../src/kernel/devices/dev_fb0.c)*

```c
static kern_file_ops_t g_dev_fb0_fops = {
    .read    = NULL,  /* 帧缓冲不支持读取 */
    .write   = dev_fb0_write,
    .ioctl   = dev_fb0_ioctl,
    .release = dev_fb0_release,
};
```

#### 写入协议

fb0 采用**命令帧**协议，每条写入是可变长度的命令：

| 命令字节 | 名称 | 数据 | 说明 |
|----------|------|------|------|
| 0x01 | `DEV_FB_CMD_PIXEL` | 2B x + 2B y + 2B color | 绘制单个像素 |
| 0x02 | `DEV_FB_CMD_FILL_RECT` | 2B x + 2B y + 2B w + 2B h + 2B color | 填充矩形 |
| 0x03 | `DEV_FB_CMD_CLEAR` | 2B color（当前忽略） | 清屏 |
| 0x04 | `DEV_FB_CMD_FLUSH` | 无 | 将后台缓冲刷新到屏幕 |

#### 中文伪代码拆解

```
设备 fb0 写入流程:

    收到缓冲区 [01 0A 00 14 00 00 FF ...]
    解析命令字节 0x01 (CMD_PIXEL)
    提取 x=0x000A (10), y=0x0014 (20), color=0xFF00
    调用 hal_display_draw_pixel(10, 20, 0xFF00)

    下一帧: 0x10 (CMD_FLUSH)
    调用 hal_display_flush()    → 后台缓冲 → pushSprite → 屏幕
```

**为什么用命令协议而非直接像素数组？**

80×160 = 12,800 像素 × 2 字节 = 25KB 仅一帧。通过命令协议，只需要传输变化的像素和图形操作，典型的一帧 UI 更新远小于 25KB。

#### ioctl 查询

*📄 Source: [dev_fb0.c](../../src/kernel/devices/dev_fb0.c#L70-L87)*

```c
switch (cmd) {
case DEV_FB_IOCTL_GET_WIDTH:
    return (int)SCREEN_WIDTH;
case DEV_FB_IOCTL_GET_HEIGHT:
    return (int)SCREEN_HEIGHT;
case DEV_FB_IOCTL_SET_ROTATION:
    /* 硬件环境: 保留接口，当前未实现 */
    return KERN_OK;
}
```

### /dev/input0 — 按键输入设备

*📄 Source: [devices/dev_input0.c](../../src/kernel/devices/dev_input0.c#L1-L80)*

```c
static kern_file_ops_t g_dev_input0_fops = {
    .read    = dev_input0_read,
    .write   = NULL,
    .ioctl   = dev_input0_ioctl,
    .release = dev_input0_release,
};
```

#### 读取协议

每次 `read` 返回固定 6 字节的按键事件结构：

*📄 Source: [dev_input0.h](../../src/kernel/devices/dev_input0.h#L20-L31)*

```c
#define DEV_INPUT_EVENT_SIZE 6

typedef struct {
    uint8_t  button;     /* 按键编号：0=BtnA, 1=BtnB */
    uint8_t  event;      /* 事件类型：见 hal_event_t */
    uint32_t timestamp;  /* 事件发生时间戳（毫秒） */
} dev_input_event_t;
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `button` | `uint8_t` | `0` = BtnA, `1` = BtnB |
| `event` | `uint8_t` | `HAL_EVENT_NONE`(0), `HAL_EVENT_SHORT_PRESS`(1), `HAL_EVENT_LONG_PRESS`(2), `HAL_EVENT_DOUBLE_CLICK`(3) |
| `timestamp` | `uint32_t` | `hal_get_ticks()` 返回的系统运行毫秒数 |

#### 中文伪代码拆解

```
按键事件读取机制：

    hal_input_update() → 检测到 BtnA 短按
      ↓
    dev_input_event_t event = {button=0, event=1, timestamp=12345}
      ↓
    用户态任务调用 kern_read(fd_input0, &event, 6)
      ↓
    dev_input0_read() 轮询 HAL 层两个按键
      ↓
    返回第一个检测到的事件（6 字节）

读取语义：
    - 非阻塞轮询：每次 read 返回当前时刻第一个有事件的按键
    - 若无事件，返回全零事件（HAL_EVENT_NONE）
    - 缓冲区长度不足 6 字节返回 KERN_EINVAL
```

### /dev/ttyS0 — 串口设备

*📄 Source: [devices/dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp)*

```c
static kern_file_ops_t g_dev_ttyS0_fops = {
    .read    = dev_ttyS0_read,
    .write   = dev_ttyS0_write,
    .ioctl   = NULL,       /* 当前无 ioctl */
    .release = dev_ttyS0_release,
};
```

ttyS0 封装 ESP32 的 USB-CDC 串口：

- `read`：从 RX 环形缓冲区读取数据，**非阻塞**（缓冲区为空时返回 0）
- `write`：写入 TX 环形缓冲区，由 `dev_ttyS0_poll()` 异步发送到硬件串口
- `ioctl`：`NULL`（当前不支持）

#### 中文伪代码拆解

```
ttyS0 数据流：

输入方向:
    USB串口数据 → dev_ttyS0_poll() → RX ring buffer → kern_read → 用户态任务

输出方向:
    用户态任务 → kern_write → TX ring buffer → dev_ttyS0_poll() → Serial.write() → 终端

关键设计：
    - 所有硬件 Serial 访问都在 dev_ttyS0_poll() 中执行（由主 loop 调用）
    - 任务的 read/write 仅操作内存环形缓冲区，避免 FreeRTOS 上下文冲突
    - 当 serial_input/serial_monitor/flasher 活跃时，poll() 跳过 RX，避免数据竞争
```

---

## 设备驱动模式

所有设备统一遵循同一个"设备驱动模式"：

```
初始化:   在 kern_devices_init() 中调用注册函数
         ├→ kern_dev_register("xxx", xxx_fops, KERN_FILE_CHRDEV, NULL)
         │    └→ kern_dentry_register("/dev/xxx", inode)
         │         └→ inode->fops = xxx_fops
         └→ 或 kern_devfs_register_device(&g_xxx_dev)  // 新版 API

运行时:   VFS 层调用 xxx_fops 函数
         ├→ xxx_open   (可选，分配打开状态)
         ├→ xxx_read   (读数据)
         ├→ xxx_write  (写数据)
         ├→ xxx_ioctl  (控制命令)
         └→ xxx_release (关闭时清理)

清理:    释放设备私有内存
```

---

## 与其他组件的关系

- **kern_vfs**：设备文件统一通过 VFS 树访问
- **kern_devfs**：设备注册到 `/dev` 目录
- **HAL 层**：fb0 调用 `hal_display_*`，input0 调用 `hal_input_*`，ttyS0 调用 `Serial.*`
- **kern_shell**：Shell 通过 ttyS0 读写终端；通过 fb0 的 `echo` 重定向控制显示

---

> **See Also:** [devfs](kern-devfs.md) | [VFS](kern-vfs.md) | [设备驱动模型](kern-device-model.md) | [Shell](kern-shell.md) | [HAL 显示驱动](../hal/display.md) | [HAL 输入系统](../hal/input.md)
