# 物理设备驱动（Kern Devices）

> **Parent:** [内核总览](index.md) | **Related:** [devfs](kern-devfs.md), [VFS](kern-vfs.md), [Shell](kern-shell.md)

## 概述

`kern_devices` 模块负责将物理硬件（传感器、显示屏、按键、串口）注册为 `/dev/*` 下的虚拟文件节点。每个设备实现 `kern_file_ops_t` 函数表，使得用户态任务可以通过 `sys_read`、`sys_write`、`sys_ioctl` 访问硬件。

设备统一在 `kern_devices_init()` 中注册，初始化顺序：fb0、input0、ttyS0。

---

## 关键概念

### 设备注册中心

*📄 Source: [devices/kern_devices.c](../../src/kernel/devices/kern_devices.c#L1-L40)*

```c
void kern_devices_init(void)
{
    kern_init();         /* 保证内核已启动 */
    kern_devfs_init();   /* 保证 /dev 目录存在 */
    kern_vfs_init();     /* 保证 VFS 树可用 */

    kern_dev_fb0_init();
    kern_dev_input0_init();
    kern_dev_ttyS0_init();
}
```

每个设备 init 函数负责：创建 VFS 节点 + 设置 `file_ops` + 初始化硬件状态。

### /dev/fb0 — 帧缓冲设备

*📄 Source: [devices/dev_fb0.c](../../src/kernel/devices/dev_fb0.c)*

```c
static struct kern_file_ops fb0_fops = {
    .open    = fb0_open,
    .read    = NULL,        /* 帧缓冲只写 */
    .write   = fb0_write,   /* 命令协议写入 */
    .ioctl   = fb0_ioctl,   /* 查询尺寸 */
    .release = NULL,
};
```

#### 写入协议

fbo 采用**命令帧**协议，每条写入是可变长度的命令：

| 命令字节 | 名称 | 数据 | 说明 |
|----------|------|------|------|
| 0x01 | CMD_PIXEL | 2B x + 2B y + 2B color | 绘制单个像素 |
| 0x02 | CMD_FILL | 2B x + 2B y + 2B w + 2B h + 2B color | 填充矩形 |
| 0x03 | CMD_LINE | 2B x0 + 2B y0 + 2B x1 + 2B y1 + 2B color | 绘制直线 |
| 0x04 | CMD_TEXT | 2B x + 2B y + 变长字符串 | 绘制文本 |
| 0x05 | CMD_CLEAR | 2B color | 清屏 |
| 0x10 | CMD_FLUSH | 无 | 将后台缓冲刷新到屏幕 |

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

```c
case 1: return width;   /* 80 or 160 depending on rotation */
case 2: return height;  /* 160 or 80 */
```

### /dev/input0 — 按键输入设备

*📄 Source: [devices/dev_input0.c](../../src/kernel/devices/dev_input0.c#L1-L75)*

```c
static struct kern_file_ops input0_fops = {
    .open    = input0_open,
    .read    = input0_read,       /* 读取按键事件 */
    .write   = NULL,              /* 不可写 */
    .ioctl   = input0_ioctl,      /* 查询缓冲区状态 */
    .release = NULL,
};
```

#### 读取协议

每次 `read` 返回固定 6 字节的按键事件结构：

```c
typedef struct {
    uint8_t  state;   /* 0=释放 1=按下 */
    uint8_t  button;  /* 0=BtnA 1=BtnB */
    uint32_t tick_ms; /* 事件时间戳 */
} kern_key_event_t;
```

#### 中文伪代码拆解

```
按键事件缓冲机制：

    hal_input_update() → 检测到 BtnA 按下
      ↓
    kern_key_event_t event = {state=1, button=0, tick_ms=12345}
      ↓
    写入环形缓冲区（input0_buf[]
      ↓
    用户态任务调用 sys_read(fd_input0, &event, 6)
      ↓
    从环形缓冲读出一个事件
    返回 6（字节）

读取语义：
    - 缓冲区为空 → 阻塞（blocked_on = 当前任务等待）
    - 缓冲区有数据 → 复制最早的事件并返回
    - 多个打开 → 每个打开都获得独立的事件流副本
```

### /dev/ttyS0 — 串口设备

*📄 Source: [devices/dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp)*

```c
static struct kern_file_ops ttyS0_fops = {
    .open    = ttyS0_open,
    .read    = ttyS0_read,      /* 读取字符（阻塞） */
    .write   = ttyS0_write,     /* 发送字符 */
    .ioctl   = ttyS0_ioctl,     /* 设置 TX 回调 */
    .release = ttyS0_release,
};
```

ttyS0 封装 ESP32 的 USB-CDC 串口：

- `read`：从 char 环形缓冲读取一个字节，空时阻塞
- `write`：调用 `Serial.write()` 发送数据
- `ioctl`：设置 TX 回调函数（用于硬件 UART 的异步发送）

#### 中文伪代码拆解

```
ttyS0 数据流：

输入方向:
    USB串口数据 → 中断触发 → ttyS0_rx_buffer → sys_read → 用户态任务

输出方向:
    用户态任务 → sys_write → Serial.write() → USB串口 → 终端

初始化:
    Serial.begin(115200) → 设置波特率 → 注册 RX ISR
```

---

## 设备驱动模式

所有设备统一遵循同一个"设备驱动模式"：

```
初始化:   kern_dev_xxx_init()
         ├→ kern_vfs_create("/dev/xxx", S_IFCHR)
         ├→ inode->i_dev = 设备私有状态
         ├→ inode->i_fops = xxx_fops
         └→ 硬件初始化

运行时:   VFS 层调用 xxx_fops 函数
         ├→ xxx_open   (可选，分配打开状态)
         ├→ xxx_read   (读数据，无数据则阻塞)
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

> **See Also:** [devfs](kern-devfs.md) | [VFS](kern-vfs.md) | [Shell](kern-shell.md) | [HAL 显示驱动](../hal/display.md) | [HAL 输入系统](../hal/input.md)
