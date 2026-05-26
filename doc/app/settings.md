# 设置管理模块（Settings）

> **Parent:** [知识地图](../index.md) | **Related:** [App 初始化](app-init.md), [核心引擎](../ui/core.md)

## 概述

`settings` 模块负责管理所有用户可配置的设置项，包括亮度、动画速度、动画开关和屏幕方向。它提供统一的加载、保存和值转换接口，将 UI 中的"档位"（1-10）与实际硬件值分离。

---

## 关键概念

### 档位与硬件值的分离

UI 控件（如滑条）只操作**档位值**（`g_brightness_level`，范围 1-10），而实际应用到硬件时通过转换函数计算：

```c
int16_t brightness = g_brightness_level * 10;              /* 百分比 10-100 */
uint8_t  hw_value  = (brightness * 255) / 100;             /* 硬件寄存器值 0-255 */
int16_t anim_speed = 40 + g_anim_speed_level * 5;          /* 实际速度 45-95 */
```

这种分离的好处：
- UI 层只关心档位，不关心硬件细节
- 存储层只存档位，向后兼容简单
- 转换逻辑集中在一处，便于调整

---

## 模块结构

*📄 Source: [settings.h](../../src/app/settings/settings.h)*

```c
extern int16_t g_brightness_level;
extern int16_t g_anim_speed_level;
extern bool    g_anim_enabled;
extern int16_t g_screen_rotation_level;
```

### 中文伪代码拆解

```
结构体 设置状态 {
    亮度档位       /* 1-10，对应 10%-100% */
    动画速度档位   /* 1-10，对应 45-95 */
    动画开关       /* true/false */
    屏幕方向档位   /* 1=竖屏, 2=横屏 */
}

/* 全局变量声明（定义在 settings.c 中） */
亮度档位 = 5      /* 默认值 50% */
动画速度档位 = 5  /* 默认值 70 */
动画开关 = true
屏幕方向档位 = 2  /* 默认横屏 */
```

---

## 核心 API

### 从存储加载

*📄 Source: [settings.c](../../src/app/settings/settings.c#L19-L55)*

```c
void settings_load_from_storage(void);
```

从 NVS（ESP32）或桩实现（Native 测试）恢复所有设置值。处理以下兼容性问题：

1. **亮度**：旧格式直接存储百分比（如 50），新格式存储档位（1-10）。加载时自动检测并转换。
2. **动画速度**：旧格式直接存储速度值（40-95），新格式存储档位（1-10）。加载时自动转换。
3. **屏幕旋转**：旧格式用 0/1/2/3 表示方向，新格式用 1/2 表示档位。通过 `resolve_rotation_level()` 映射。

### 值转换

*📄 Source: [settings.c](../../src/app/settings/settings.c#L57-L65)*

```c
int16_t settings_brightness_hw_value(void);   /* 档位 → 0-255 硬件值 */
int16_t settings_anim_speed_value(void);      /* 档位 → 45-95 实际速度 */
```

---

## 初始化流程

```
main.cpp setup():
    storage_init()                /* 先初始化存储层 */
    settings_load_from_storage()  /* 恢复所有设置 */
    M5.Display.setBrightness(     /* 应用亮度 */
        settings_brightness_hw_value()
    )
    g_anim_speed = settings_anim_speed_value()  /* 应用动画速度 */
    M5.Display.setRotation(...)   /* 应用屏幕方向 */
```

---

## 变更回调

设置值变更时，UI 控件通过回调通知 main.cpp 应用新值：

| 设置项 | 回调 | 应用操作 |
|--------|------|----------|
| 亮度 | `on_brightness_change_cb()` | `M5.Display.setBrightness(hw)` |
| 动画速度 | `on_anim_speed_change_cb()` | 更新 `g_anim_speed` |
| 动画开关 | `on_anim_enabled_change_cb()` | 更新 `g_anim_enabled` |
| 屏幕方向 | `on_screen_rotation_change_cb()` | `M5.Display.setRotation()` |

> **为什么回调在 main.cpp 中**：这些回调需要调用 `M5.Display` 等 C++ Arduino API，而 main.cpp 是唯一的 C++ 入口文件。

---

> **See Also:** [App 初始化](app-init.md) | [存储模块](storage.md) | [屏幕旋转映射](../developer-guide.md)
