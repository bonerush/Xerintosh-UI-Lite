# 版本信息管理 (`kern_version.h`)

> 源码: `src/kernel/kern_version.h`

## 概述

`kern_version.h` 提供 Xeros 内核的**统一版本定义**，所有需要显示版本信息的位置（`/proc/version`、`uname`、`/proc/developer` 等）都引用此头文件，避免版本号散落各处。

## 定义

```c
#define XEROS_VERSION_MAJOR  0
#define XEROS_VERSION_MINOR  2
#define XEROS_VERSION_PATCH  0
#define XEROS_VERSION_STRING "0.2.0"

#define XEROS_DEVELOPER      "YukiSala"
#define XEROS_CODENAME       "M5Stick-P1"
#define XEROS_PLATFORM       "ESP32-PICO"  /* NATIVE_TEST 时自动切换为 "native (x86_64)" */
```

## 引用位置

| 文件 | 用途 |
|------|------|
| `/proc/version` | 完整版本信息（版本号 + 开发者 + 平台 + 编译时间） |
| `/proc/developer` | 开发者与项目信息 |
| `uname` 命令 | 简略系统信息（版本号 + 平台 + 编译时间） |

## 版本号规则

遵循语义化版本（SemVer）：
- **MAJOR**: 不兼容的架构变更（如去 FreeRTOS）
- **MINOR**: 向后兼容的新功能（如新的文件系统、App）
- **PATCH**: 向后兼容的 Bug 修复

当前版本 `0.2.0` 表示：
- 0: 实验阶段（API 不稳定）
- 2: 引入 GPIO 桥接、虚任务、任务管理器等新功能
- 0: 无已发布的补丁
