# 系统时钟（HAL System）

> **Parent:** [知识地图](../index.md) | **Related:** [显示驱动](display.md), [输入系统](input.md)

## 概述

系统 HAL 提供最基础的系统服务：毫秒级时钟（`tick`）和延时。由于本项目需要同时支持 **ESP32 真机** 和 **桌面 Native 测试**，本层做了最小化的双实现。

---

## 关键概念

### API 设计

*📄 Source: [hal_system.h](../../src/hal/hal_system.h#L18-L37)*

```c
extern void hal_system_init(void);
extern uint32_t hal_get_ticks(void);
extern void hal_delay_ms(uint32_t ms);
```

- `hal_system_init()`：初始化时钟基准（Native 环境下记录起始时间点）
- `hal_get_ticks()`：返回自启动以来的毫秒数（32-bit，约 49 天回绕）
- `hal_delay_ms()`：阻塞延时（Native 测试中空实现）

### 真机实现（ESP32 Arduino）

*📄 Source: [hal_system.cpp](../../src/hal/hal_system.cpp#L44-L68)*

```c
#include <Arduino.h>

void hal_system_init(void) {}
uint32_t hal_get_ticks(void) { return millis(); }
void hal_delay_ms(uint32_t ms) { delay(ms); }
```

真机上直接透传 Arduino 的 `millis()` 和 `delay()`，零开销。

### Native 测试实现

*📄 Source: [hal_system.cpp](../../src/hal/hal_system.cpp#L13-L42)*

```c
#include <chrono>

static auto g_start_time = std::chrono::steady_clock::now();

void hal_system_init(void) {
    g_start_time = std::chrono::steady_clock::now();
}

uint32_t hal_get_ticks(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
}

void hal_delay_ms(uint32_t ms) {
    (void)ms;
}
```

#### 中文伪代码拆解

```
变量 启动时间点 =  steady_clock.当前时间()

函数 系统初始化() {
    启动时间点 = steady_clock.当前时间()
}

函数 获取毫秒Tick() {
    当前时间 = steady_clock.当前时间()
    经过时间 = 当前时间 - 启动时间点
    return 经过时间.转为毫秒()
}

函数 延时(毫秒) {
    // Native 测试不需要阻塞，空实现
}
```

**为什么用 `.cpp` 而不是 `.c`**：Native 实现需要 `<chrono>`，这是 C++ 标准库。PlatformIO 对 `.c` 文件使用 C 编译器，无法识别 C++ 头文件。因此整个 HAL 层统一使用 `.cpp` 扩展名，通过 `extern "C"` 保持 C 链接。

---

## 与其他组件的关系

- **hal_input**：`hal_input_get_event()` 内部使用 `millis()`（真机）或测试注入时间戳计算按住时长
- **ui_core**：动画系统使用 `get_ticks()`（实际为宏映射到 `hal_get_ticks()`）计算弹窗超时
- **native_main.cpp**：测试入口先调用 `hal_system_init()` 建立时间基准

---

> **See Also:** [显示驱动](display.md) | [输入系统](input.md)
