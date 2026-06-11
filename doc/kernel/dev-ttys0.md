# /dev/ttyS0 串口设备

> **Parent:** [内核子系统总览](index.md) | **Related:** [物理设备驱动](kern-devices.md), 串口输入, [串口监视器](../app/serial-monitor.md)

## 概述

`/dev/ttyS0` 是 Xeros 内核的**串口字符设备**，将硬件串口（`Serial` / `std::cin`）映射为 VFS 文件操作。它通过独立的收发环形缓冲区解耦硬件读写与任务调用，避免 FreeRTOS 任务上下文中的串口冲突。

---

## 架构设计

```
用户程序 / Shell
    |
    | read(/dev/ttyS0) / write(/dev/ttyS0)
    v
/dev/ttyS0 驱动
    |         ^
    | read    | write
    v         |
  RX 环形缓冲区   TX 环形缓冲区
    ^         |
    | poll    | poll
    v         v
Arduino Serial  /  std::cin (Native)
```

**关键设计**：所有对 `Serial` 的硬件访问都在 `dev_ttyS0_poll()` 中执行（由主循环调用），任务的 `read()`/`write()` 仅操作内存中的环形缓冲区。这避免了在任务上下文中直接调用 Arduino API 导致的 FreeRTOS 冲突。

---

## 数据缓冲区

*📄 Source: [dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp#L20-L42)*

### 硬件环境（ESP32）

```c
#define TTY_RX_BUF_SIZE  512
#define TTY_TX_BUF_SIZE  512

static char  g_rx_buf[TTY_RX_BUF_SIZE];
static volatile int g_rx_head = 0;
static volatile int g_rx_tail = 0;
static volatile int g_rx_count = 0;

static char  g_tx_buf[TTY_TX_BUF_SIZE];
static volatile int g_tx_head = 0;
static volatile int g_tx_tail = 0;
static volatile int g_tx_count = 0;
```

### Native 测试环境

```c
#define TTY_BUF_SIZE  512

static char  g_tty_buf[TTY_BUF_SIZE];   /* 单缓冲区（Native 只读） */
static int   g_tty_head = 0;
static int   g_tty_tail = 0;
static int   g_tty_count = 0;
```

**差异说明**：Native 环境使用 `std::cin` 模拟串口输入，TX 数据也写入同一缓冲区（`write()` 将数据放入缓冲区，`read()` 从中读出）。

---

## read 接口

*📄 Source: [dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp#L46-L68)*

```c
static ssize_t dev_ttyS0_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;

#ifdef NATIVE_TEST
    if (g_tty_count == 0) return 0;
    size_t total = 0;
    while (total < len && g_tty_count > 0) {
        buf[total++] = g_tty_buf[g_tty_tail];
        g_tty_tail = (g_tty_tail + 1) % TTY_BUF_SIZE;
        g_tty_count--;
    }
    return (ssize_t)total;
#else
    size_t total = 0;
    while (total < len && g_rx_count > 0) {
        buf[total++] = g_rx_buf[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % TTY_RX_BUF_SIZE;
        __atomic_fetch_sub(&g_rx_count, 1, __ATOMIC_RELAXED);
    }
    return (ssize_t)total;
#endif
}
```

### 中文伪代码拆解

```
函数 串口读取(文件, 缓冲区, 请求长度) {
#ifdef Native测试
    if (缓冲区无数据) return 0
    已读取 = 0
    while (已读取 < 请求长度 且 缓冲区有数据) {
        缓冲区[已读取] = 接收缓冲区[尾部指针]
        尾部指针 = (尾部指针 + 1) % 缓冲区大小
        缓冲区计数--
        已读取++
    }
    return 已读取字节数
#else
    已读取 = 0
    while (已读取 < 请求长度 且 RX缓冲区有数据) {
        缓冲区[已读取] = RX缓冲区[尾部指针]
        尾部指针 = (尾部指针 + 1) % 缓冲区大小
        原子操作: 缓冲区计数减1
        已读取++
    }
    return 已读取字节数    // 可能为0（非阻塞）
#endif
}
```

**核心思想**：非阻塞读取。如果环形缓冲区为空，立即返回 `0`，不等待。硬件环境下使用 `__atomic_fetch_sub` 保证计数操作的原子性。

---

## write 接口

*📄 Source: [dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp#L72-L95)*

```c
static ssize_t dev_ttyS0_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;

#ifdef NATIVE_TEST
    size_t total = 0;
    while (total < len) {
        if (g_tty_count >= TTY_BUF_SIZE) break;
        g_tty_buf[g_tty_head] = buf[total++];
        g_tty_head = (g_tty_head + 1) % TTY_BUF_SIZE;
        g_tty_count++;
    }
    return (ssize_t)total;
#else
    size_t total = 0;
    while (total < len && g_tx_count < TTY_TX_BUF_SIZE) {
        g_tx_buf[g_tx_head] = buf[total];
        g_tx_head = (g_tx_head + 1) % TTY_TX_BUF_SIZE;
        __atomic_fetch_add(&g_tx_count, 1, __ATOMIC_RELAXED);
        total++;
    }
    return (ssize_t)total;
#endif
}
```

### 中文伪代码拆解

```
函数 串口写入(文件, 数据, 请求长度) {
#ifdef Native测试
    已写入 = 0
    while (已写入 < 请求长度) {
        if (缓冲区已满) break
        缓冲区[头部] = 数据[已写入]
        头部 = (头部 + 1) % 大小
        计数++
        已写入++
    }
    return 已写入字节数
#else
    已写入 = 0
    while (已写入 < 请求长度 且 TX缓冲区未满) {
        TX缓冲区[头部] = 数据[已写入]
        头部 = (头部 + 1) % 大小
        原子操作: 缓冲区计数加1
        已写入++
    }
    return 已写入字节数    // 缓冲区满时可能小于请求长度
#endif
}
```

---

## 轮询函数

*📄 Source: [dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp#L122-L156)*

```c
void dev_ttyS0_poll(void)
{
    /*
     * RX: 从硬件串口读取数据，写入环形缓冲区供任务消费。
     *
     * 在以下三种情况下跳过 RX，将字符留在硬件 Serial 缓冲区:
     * 1. serial_input 正在等待密码/配对码（由 serial_poll() 直接消费）
     * 2. 串口监视器正在运行（由 serial_monitor_update() 直接消费）
     * 3. 烧录器有线桥接激活（由 g_flasher_bridge_active 标志控制）
     * 否则 Serial 字节会被此处消耗并进入 ring buffer，
     * Shell 和 serial_input/serial_monitor/flasher 会竞争同一份数据。
     */
    int rx_limit = 32;
    if (!serial_input_is_waiting() && !serial_monitor_is_active() && !g_flasher_bridge_active) {
        while (rx_limit > 0 && Serial.available() > 0 && g_rx_count < TTY_RX_BUF_SIZE) {
            g_rx_buf[g_rx_head] = (char)Serial.read();
            g_rx_head = (g_rx_head + 1) % TTY_RX_BUF_SIZE;
            __atomic_fetch_add(&g_rx_count, 1, __ATOMIC_RELAXED);
            rx_limit--;
        }
    }

    /*
     * TX: 从环形缓冲区读取任务写入的数据，发送到硬件串口。
     * 每次轮询最多发送 64 字节。
     */
    int tx_limit = 64;
    while (tx_limit > 0 && g_tx_count > 0) {
        Serial.write((uint8_t)g_tx_buf[g_tx_tail]);
        g_tx_tail = (g_tx_tail + 1) % TTY_TX_BUF_SIZE;
        __atomic_fetch_sub(&g_tx_count, 1, __ATOMIC_RELAXED);
        tx_limit--;
    }
}
```

### 中文伪代码拆解

```
函数 串口轮询() {
    // 第一步：RX — 硬件串口 → RX 缓冲区
    // 关键：如果 serial_input、serial_monitor 或 flasher 正在活跃使用串口，
    //       则跳过，避免数据竞争
    RX上限 = 32
    if (serial_input没在等输入 且 串口监视器没运行 且 烧录器桥接未激活) {
        while (还有RX上限 且 Serial有数据 且 RX缓冲区未满) {
            从Serial读取一个字节
            存入RX缓冲区
            原子操作: RX计数加1
            RX上限--
        }
    }

    // 第二步：TX — TX 缓冲区 → 硬件串口
    TX上限 = 64
    while (还有TX上限 且 TX缓冲区有数据) {
        从TX缓冲区取出一个字节
        写入Serial
        原子操作: TX计数减1
        TX上限--
    }
}
```

**调用时机**：`dev_ttyS0_poll()` 由 `main.cpp` 的 `loop()` 或 UI 任务每帧调用，负责在"安全上下文"（非中断、非任务切换期间）执行硬件串口操作。

**数据竞争防护**：当 `serial_input` 正在等待密码/配对码输入，或 `serial_monitor` 正在运行时，`dev_ttyS0_poll()` 会跳过 RX 读取，让这两个模块直接从 `Serial` 消费数据。这避免了 Shell 和专用输入模块竞争同一份字节流。

---

## 文件操作表

*📄 Source: [dev_ttyS0.cpp](../../src/kernel/devices/dev_ttyS0.cpp#L107-L117)*

```c
static kern_file_ops_t g_dev_ttyS0_fops = {
    .read    = dev_ttyS0_read,
    .write   = dev_ttyS0_write,
    .ioctl   = NULL,       /* 当前无 ioctl */
    .release = dev_ttyS0_release,
};
```

---

## 与串口输入模块的协作

`/dev/ttyS0` 与 `serial_input` 模块共享数据流：

1. **串口输入模块**（`serial_input.cpp`）直接读取 `Serial`，处理 WiFi 密码、蓝牙配对码等特殊输入场景
2. **Shell 任务**通过 `/dev/ttyS0` 读取命令行输入
3. **串口监视器 App**通过 `serial_monitor_update()` 直接消费原始串口数据

这种分层设计使得不同场景可以使用不同的输入抽象：
- 交互式 Shell → VFS `/dev/ttyS0`
- 密码输入 → 直接 `Serial.read()`（需要隐藏回显，绕过 ring buffer）
- 数据监视 → 串口监视器直接消费（需要原始字节流，绕过 ring buffer）

---

> **See Also:** [物理设备驱动](kern-devices.md) | 串口输入 | [串口监视器](../app/serial-monitor.md) | [VFS 核心](kern-vfs.md)
