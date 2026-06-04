# GPIO 桥接文件系统

> **Parent:** [内核总览](index.md) | **Related:** [VFS 核心](kern-vfs.md), [/proc 与 /sys](kern-procfs-sysfs.md)

## 概述

`kern_gpiofs` 将 M5Stick-C 的 GPIO 引脚状态以文件形式映射到 `/sys/gpio/` 目录下，遵循"一切皆文件"哲学。每个物理引脚对应一个可读文件（输出引脚也可写），另有 `/sys/gpio/list` 汇总文件展示所有引脚状态。

---

## 文件结构

```
/sys/gpio/
├── list           ← 只读：所有引脚状态汇总表
├── 0              ← 读写：GPIO0（BOOT / I2C SDA）
├── 25             ← 读写：GPIO25（Speaker DAC）
├── 26             ← 读写：GPIO26（LCD Backlight）
├── 32             ← 读写：GPIO32（Grove SDA）
├── 33             ← 读写：GPIO33（Grove SCL）
├── 36             ← 只读：GPIO36（Button A，仅输入）
└── 37             ← 只读：GPIO37（Button B，仅输入）
```

## 引脚定义表

*📄 Source: [kern_gpiofs.c](../../src/kernel/kern_gpiofs.c#L40-L48)*

```c
static const gpiofs_pin_desc_t g_gpio_pins[] = {
    {  0, "BOOT / I2C SDA",    true  },
    { 25, "Speaker DAC",       true  },
    { 26, "LCD Backlight",     true  },
    { 32, "Grove SDA",         true  },
    { 33, "Grove SCL",         true  },
    { 36, "Button A (RTC)",    false },   /* 仅输入 */
    { 37, "Button B (RTC)",    false },   /* 仅输入 */
};
```

| 引脚 | 功能 | 可输出 | 备注 |
|------|------|--------|------|
| GPIO0 | BOOT / I2C SDA | ✅ | 启动配置引脚，谨慎使用 |
| GPIO25 | Speaker DAC | ✅ | 扬声器 DAC 输出 |
| GPIO26 | LCD Backlight | ✅ | 屏幕背光控制 |
| GPIO32 | Grove SDA | ✅ | Grove 接口 I2C 数据线 |
| GPIO33 | Grove SCL | ✅ | Grove 接口 I2C 时钟线 |
| GPIO36 | Button A (RTC) | ❌ | RTC 域，仅输入 |
| GPIO37 | Button B (RTC) | ❌ | RTC 域，仅输入 |

---

## 关键概念

### 架构：共享 fops + private_data 分派

与 procfs/sysfs 一样，gpiofs 使用**一个全局函数表** + `inode->private_data` 区分文件：

```
g_gpiofs_fops  (所有 /sys/gpio 文件共享)
  ├── .read  = gpiofs_read()
  │     └→ private_data == -1        → gpiofs_list_generate()  (汇总表)
  │     └→ private_data == 引脚号     → gpiofs_pin_generate()  (单个引脚)
  │
  └── .write = gpiofs_write()
        └→ private_data == -1        → KERN_EACCES  (汇总表只读)
        └→ can_output == false       → KERN_EACCES  (输入引脚只读)
        └→ 解析 "0"/"1" → gpio_set_output(pin, value)
```

### 硬件访问

*📄 Source: [kern_gpiofs.c](../../src/kernel/kern_gpiofs.c#L74-L98)*

```c
#ifndef NATIVE_TEST
static int gpio_read(uint8_t pin)     { return digitalRead(pin); }
static void gpio_set_output(uint8_t pin, int value) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, value ? HIGH : LOW);
}
#else
static int gpio_read(uint8_t pin)     { (void)pin; return 0; }
static void gpio_set_output(...)      { /* stub */ }
#endif
```

硬件访问使用 Arduino `digitalRead()` / `digitalWrite()` API。首次写入输出引脚时自动设置 `pinMode(pin, OUTPUT)`。Native 测试环境下所有 GPIO 操作返回 0（stub）。

### 初始化流程

*📄 Source: [kern_gpiofs.c](../../src/kernel/kern_gpiofs.c#L238-L259)*

```
kern_gpiofs_init():
  kern_vfs_mkdir("/sys/gpio")              ← 创建目录
  REGISTER_GPIO_FILE("list", -1)           ← /sys/gpio/list
  for each pin in g_gpio_pins:
    REGISTER_GPIO_FILE("引脚号", 引脚号)    ← /sys/gpio/0, /sys/gpio/25, ...
```

`REGISTER_GPIO_FILE` 是一个宏，内部：`calloc` inode → 设置 `fops=&g_gpiofs_fops`, `private_data=(void*)引脚号或-1` → `kern_dentry_register()`。

---

## 使用示例

```bash
# 查看所有引脚汇总表
cat /sys/gpio/list
# 输出:
# PIN  DIR   VAL  FUNC
# ---  ----  ---  ----
# 0    IN    1    BOOT / I2C SDA
# 25   IN    0    Speaker DAC
# ...

# 查看单个引脚
cat /sys/gpio/36
# 输出: pin=36 direction=in value=1 function="Button A (RTC)"

# 设置输出引脚
echo 1 > /sys/gpio/25   # GPIO25 输出高电平
echo 0 > /sys/gpio/26   # GPIO26 输出低电平（关闭背光）

# 对输入引脚写入会返回权限错误
echo 1 > /sys/gpio/36
# 返回: KERN_EACCES (input-only pin)
```

---

## 与其他组件的关系

- **kern_vfs**：通过 `kern_vfs_mkdir` / `kern_dentry_register` 挂载到 VFS 树
- **kern_sysfs**：gpiofs 挂载在 `/sys/gpio/`，与 `/sys/brightness` 等同属 `/sys` 子树
- **Shell**：`cat /sys/gpio/list`、`echo 1 > /sys/gpio/25` 通过 VFS 操作
- **HAL 层**：通过 Arduino `digitalRead`/`digitalWrite` 访问 ESP32 硬件

---

> **See Also:** [VFS 核心](kern-vfs.md) | [/proc 与 /sys](kern-procfs-sysfs.md) | [设备驱动模型](kern-device-model.md)
