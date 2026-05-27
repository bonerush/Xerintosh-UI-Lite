# GPIO 桥接文件系统 (`/sys/gpio`)

> 源码: `src/kernel/kern_gpiofs.c`, `src/kernel/kern_gpiofs.h`

## 概述

`kern_gpiofs` 将 M5Stick-C 的 GPIO 引脚状态以文件形式映射到 `/sys/gpio/` 下，遵循"一切皆文件"哲学。

## 文件结构

```
/sys/gpio
├── list           ← 只读：所有引脚状态汇总表
├── 0              ← 读写：GPIO0（BOOT / I2C SDA）
├── 25             ← 读写：GPIO25（Speaker DAC）
├── 26             ← 读写：GPIO26（LCD Backlight）
├── 32             ← 读写：GPIO32（Grove SDA）
├── 33             ← 读写：GPIO33（Grove SCL）
├── 36             ← 只读：GPIO36（Button A，仅输入）
└── 37             ← 只读：GPIO37（Button B，仅输入）
```

## 读取引脚状态

```bash
# 查看所有引脚汇总表
cat /sys/gpio/list

# 查看单个引脚
cat /sys/gpio/36
# 输出: pin=36 direction=in value=1 function="Button A (RTC)"
```

## 写入输出引脚

```bash
# 设置 GPIO25 输出高电平
echo 1 > /sys/gpio/25

# 设置 GPIO25 输出低电平
echo 0 > /sys/gpio/25

# 对输入引脚写入会返回权限错误
echo 1 > /sys/gpio/36
# 输出: gpio: permission denied (input-only)
```

## 引脚定义表

| 引脚 | 功能 | 可输出 |
|------|------|--------|
| GPIO0  | BOOT / I2C SDA | 是 |
| GPIO25 | Speaker DAC    | 是 |
| GPIO26 | LCD Backlight  | 是 |
| GPIO32 | Grove SDA      | 是 |
| GPIO33 | Grove SCL      | 是 |
| GPIO36 | Button A (RTC) | 否 |
| GPIO37 | Button B (RTC) | 否 |

## 实现细节

- **硬件访问**: ESP32 使用 Arduino `digitalRead()`/`digitalWrite()` API
- **Native 测试**: 所有 GPIO 操作返回 0（stub）
- **输出引脚初始化**: 首次写入时自动设置 `pinMode(pin, OUTPUT)`
- **文件类型**: 仅输入引脚文件为只读（`write` 返回 `KERN_EACCES`）
- **初始化**: `kern_gpiofs_init()` 在 `deferred_kernel_init()` 中调用，位于 sysfs 之后
