# HAL 层索引

> **Parent:** [知识地图](../index.md)

## 概述

HAL（Hardware Abstraction Layer）层封装了所有硬件相关操作，为上层 UI 框架和内核提供统一的抽象接口。

## 模块列表

| 模块 | 文档 | 源码 | 说明 |
|------|------|------|------|
| 显示驱动 | [display.md](display.md) | `src/hal/hal_display.cpp/h` | TFT 双缓冲 / Native 内存帧缓冲、绘制原语 |
| 输入系统 | [input.md](input.md) | `src/hal/hal_input.cpp/h` | 按键消抖 + 短按/长按/双击状态机 |
| 系统时钟 | [system.md](system.md) | `src/hal/hal_system.cpp/h` | `millis()` 封装、`std::chrono` 桌面回退 |

## 设计原则

- **双平台支持**：每个 HAL 模块在 `#ifdef NATIVE_TEST` 下提供桌面桩实现，在 ESP32 上用真实硬件 API
- **纯 C 接口**：所有 `.h` 文件有 `extern "C"` 保护，内部 `.cpp` 可使用 C++ 库（M5GFX、Arduino）
- **无状态暴露**：`hal_` 前缀函数屏蔽内部实现细节

---

> **See Also:** [App 层](../app/index.md) | [UI 核心层](../ui/index.md) | [内核层](../kernel/index.md)
