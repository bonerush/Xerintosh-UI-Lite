# 版本信息管理（Kern Version）

> **Parent:** [内核总览](index.md) | **Related:** [内核初始化](kern-init.md), [/proc 与 /sys](kern-procfs-sysfs.md)

## 概述

`kern_version.h` 提供 Xeros 内核的**统一版本定义**，所有需要显示版本信息的位置（`/proc/version`、`uname`、`/proc/developer` 等）都引用此头文件，避免版本号散落各处。

---

## 定义

*📄 Source: [kern_version.h](../../src/kernel/kern_version.h#L20-L30)*

```c
#define XEROS_VERSION_STRING "0.2.0"

#define XEROS_DEVELOPER      "Bonerush"
#define XEROS_CODENAME       "M5Stick-P1"

#ifdef NATIVE_TEST
#define XEROS_PLATFORM       "native (x86_64)"
#else
#define XEROS_PLATFORM       "ESP32-PICO"
#endif
```

**当前版本 `0.2.0`** 表示：
- **0**: 实验阶段（API 不稳定）
- **2**: kernel-v2 架构引入（SMP 多核、资源追踪、MPU 内存保护、设备驱动模型）
- **0**: 无已发布的补丁

**平台自动切换**：`XEROS_PLATFORM` 通过 `#ifdef NATIVE_TEST` 在编译时自动选择，无需手动修改。Native 测试环境下显示 `"native (x86_64)"`，硬件编译时显示 `"ESP32-PICO"`。

---

## 引用位置

| 文件 | 用途 |
|------|------|
| `/proc/version` | 完整版本信息（版本号 + 开发者 + 平台 + 编译时间） |
| `/proc/developer` | 开发者与项目信息 |
| `uname` 命令 | 简略系统信息（版本号 + 平台 + 编译时间） |

---

> **See Also:** [内核初始化](kern-init.md) | [/proc 与 /sys](kern-procfs-sysfs.md)
