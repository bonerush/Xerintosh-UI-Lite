# /dev/input0 按键输入设备

> **Parent:** [内核子系统总览](index.md) | **Related:** [物理设备驱动](kern-devices.md), [输入系统](../hal/input.md)

## 概述

`/dev/input0` 是 Xeros 内核的**按键输入设备**，将 HAL 输入层的事件映射为 VFS 文件操作。应用程序通过 `read()` 读取结构化按键事件，通过 `ioctl()` 配置输入行为（如双击检测）。

---

## 事件结构

*📄 Source: [dev_input0.h](../../src/kernel/devices/dev_input0.h)*

```c
#define DEV_INPUT_EVENT_SIZE 6

typedef struct {
    uint8_t  button;    /* 按键编号：0=BtnA, 1=BtnB */
    uint8_t  event;     /* 事件类型：见 hal_event_t */
    uint32_t timestamp; /* 事件发生时间戳（毫秒） */
} dev_input_event_t;
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `button` | `uint8_t` | `0` = BtnA（右侧），`1` = BtnB（左侧） |
| `event` | `uint8_t` | `HAL_EVENT_NONE`(0), `HAL_EVENT_SHORT_PRESS`(1), `HAL_EVENT_LONG_PRESS`(2), `HAL_EVENT_DOUBLE_CLICK`(3) |
| `timestamp` | `uint32_t` | `hal_get_ticks()` 返回的系统运行毫秒数 |

---

## read 接口

*📄 Source: [dev_input0.c](../../src/kernel/devices/dev_input0.c#L18-L42)*

```c
static ssize_t dev_input0_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;

    if (len < DEV_INPUT_EVENT_SIZE) {
        return KERN_EINVAL;
    }

    /* 轮询两个按键，返回第一个检测到的事件 */
    dev_input_event_t ev;
    memset(&ev, 0, sizeof(ev));

    for (int btn = 0; btn < HAL_BTN_COUNT; btn++) {
        hal_event_t e = hal_input_get_event((hal_button_t)btn);
        if (e != HAL_EVENT_NONE) {
            ev.button    = (uint8_t)btn;
            ev.event     = (uint8_t)e;
            ev.timestamp = hal_get_ticks();
            break;
        }
    }

    memcpy(buf, &ev, DEV_INPUT_EVENT_SIZE);
    return DEV_INPUT_EVENT_SIZE;
}
```

### 中文伪代码拆解

```
函数 输入设备读取(文件, 缓冲区, 长度) {
    if (缓冲区长度 < 6字节) return 参数错误

    创建空事件结构

    // 轮询两个按键
    for (按键 = 0; 按键 < 按键总数; 按键++) {
        事件 = HAL获取按键事件(按键)

        if (事件 != 无事件) {
            事件.按键编号 = 按键
            事件.事件类型 = 事件
            事件.时间戳  = 获取当前Tick
            break    // 只返回第一个检测到的事件
        }
    }

    复制事件结构到缓冲区
    return 事件大小(6字节)
}
```

**核心思想**：非阻塞轮询。每次 `read()` 返回当前时刻第一个有事件的按键（如果有）。如果没有按键事件，返回一个全零事件（`HAL_EVENT_NONE`）。

---

## ioctl 接口

*📄 Source: [dev_input0.c](../../src/kernel/devices/dev_input0.c#L46-L57)*

```c
static int dev_input0_ioctl(kern_file_t *f, unsigned int cmd, unsigned long arg)
{
    (void)f;

    switch (cmd) {
    case DEV_INPUT_IOCTL_SET_DOUBLE_CLICK:
        hal_input_set_double_click_enabled(arg != 0);
        return KERN_OK;
    default:
        return KERN_ENOTTY;
    }
}
```

| ioctl | 参数 | 说明 |
|-------|------|------|
| `DEV_INPUT_IOCTL_SET_DOUBLE_CLICK` | `0` 或 `1` | 启用/禁用双击检测 |

---

## 读取示例

```c
int fd = kern_open("/dev/input0", O_RDONLY);
if (fd < 0) { /* 错误处理 */ }

for (;;) {
    char buf[DEV_INPUT_EVENT_SIZE];
    ssize_t n = kern_read(fd, buf, sizeof(buf));
    if (n == DEV_INPUT_EVENT_SIZE) {
        dev_input_event_t *ev = (dev_input_event_t *)buf;
        if (ev->event == HAL_EVENT_SHORT_PRESS) {
            printf("Btn %d short pressed at %lu ms\n",
                   ev->button, ev->timestamp);
        }
    }
    kern_yield();   /* 让出 CPU，避免忙等 */
}
```

---

## 与 HAL 输入层的关系

```
用户程序
    |
    | read(/dev/input0)
    v
/dev/input0 驱动
    |
    | hal_input_get_event()
    v
HAL 输入层（hal_input.cpp）
    |
    | M5.BtnA/BtnB 读取
    v
M5Unified 硬件抽象
```

`/dev/input0` 是 VFS 封装层，真正的按键消抖和事件状态机在 `hal_input.cpp` 中实现。设备驱动只是将 HAL 事件转换为标准文件读取接口。

---

> **See Also:** [物理设备驱动](kern-devices.md) | [输入系统](../hal/input.md)
