# 设置管理模块（Settings）

> **Parent:** [知识地图](../index.md) | **Related:** [App 初始化](app-init.md), [核心引擎](../ui/core.md)

## 概述

`settings` 模块负责管理所有用户可配置的设置项，包括亮度、动画速度、动画开关、屏幕方向和串口波特率。它提供统一的加载、保存和值转换接口，将 UI 中的"档位"（1-10）与实际硬件值分离。

---

## 关键概念

### 档位与硬件值的分离

UI 控件（如滑条）只操作**档位值**（`g_brightness_level`，范围 1-10），而实际应用到硬件时通过转换函数计算：

```c
int16_t brightness = g_brightness_level * 10;              /* 百分比 10-100 */
uint8_t  hw_value  = (brightness * 255) / 100;             /* 硬件寄存器值 0-255 */
int16_t anim_speed = 40 + g_anim_speed_level * 5;          /* 实际速度 45-90 */
```

这种分离的好处：
- UI 层只关心档位，不关心硬件细节
- 存储层只存档位，向后兼容简单
- 转换逻辑集中在一处，便于调整

---

## 模块结构

*📄 Source: [settings.h](../../src/app/settings/settings.h#L33-L39)*

```c
extern int16_t g_brightness_level;       /* 亮度等级 1-10 */
extern int16_t g_anim_speed_level;       /* 动画速度等级 1-10 */
extern int16_t g_screen_rotation_level;  /* 屏幕方向 1=竖屏, 2=横屏 */
extern bool    g_is_landscape;           /* 横屏开关：true=横屏, false=竖屏 */
extern int16_t g_serial_baud_rate;       /* 串口波特率等级 1-6 */
```

*📄 Source: [settings.c](../../src/app/settings/settings.c#L17-L21)*

```c
int16_t g_brightness_level       = 5;                        /* 默认亮度等级 5 */
int16_t g_anim_speed_level       = 5;                        /* 默认动画速度 5 */
int16_t g_screen_rotation_level  = ORIENTATION_LANDSCAPE;    /* 默认横屏 */
bool    g_is_landscape           = true;                     /* 默认横屏 */
int16_t g_serial_baud_rate       = 5;                        /* 默认波特率等级 5 = 115200 */
```

### 中文伪代码拆解

```
结构体 设置状态 {
    亮度档位       /* 1-10，对应 10%-100% */
    动画速度档位   /* 1-10，对应 45-90 */
    动画开关       /* true/false（定义在 ui_context.h） */
    横屏/竖屏开关   /* true=横屏, false=竖屏，默认横屏 */
    波特率档位     /* 1-6，对应 9600-230400 */
}

/* 全局变量定义（settings.c 中） */
亮度档位 = 5      /* 默认值 50% */
动画速度档位 = 5  /* 默认值 65 (40 + 5*5) */
动画开关 = true   /* 由 ui_context.h 提供 */
横屏开关 = true   /* 默认横屏 */
波特率档位 = 5    /* 默认值 115200 */
```

---

## 核心 API

### Getter / Setter

*📄 Source: [settings.c](../../src/app/settings/settings.c#L83-L114)*

| 函数 | 说明 |
|------|------|
| `settings_get_brightness()` / `settings_set_brightness(level)` | 亮度档位 1-10 |
| `settings_get_anim_speed()` / `settings_set_anim_speed(level)` | 动画速度档位 1-10 |
| `settings_get_rotation()` / `settings_set_rotation(level)` | 屏幕方向 1=竖屏, 2=横屏 |
| `settings_get_landscape()` / `settings_set_landscape(landscape)` | 横屏布尔开关 |
| `settings_get_baud_rate()` / `settings_set_baud_rate(level)` | 波特率档位 1-6 |

### 从存储加载

*📄 Source: [settings.c](../../src/app/settings/settings.c#L32-L79)*

```c
void settings_load_from_storage(void);
```

从 NVS（ESP32）或桩实现（Native 测试）恢复所有设置值。处理以下兼容性问题：

1. **亮度**：存储值范围 1-10 直接读取；异常值（不在 1-10 范围内）通过 `(saved_bright + 9) / 10` 向上取整并裁剪到 1-10。
2. **动画速度**：旧格式直接存储速度值（40-95，步进 5），新格式存储档位（1-10）。加载时若检测到旧格式值，用 `(saved_anim - 40) / 5` 转换。
3. **屏幕旋转**：新 key 直接存储新格式值 1/2。无效值默认回退到横屏（`ORIENTATION_LANDSCAPE`）。
4. **波特率**：1-6 直接读取，无效值回退到 5（115200）。

### 值转换

*📄 Source: [settings.c](../../src/app/settings/settings.c#L118-L164)*

```c
int16_t settings_brightness_hw_value(void);      /* 亮度档位 → 0-255 硬件值 */
int16_t settings_anim_speed_value(void);         /* 速度档位 → 45-95 实际速度 */
int32_t settings_serial_baud_hw_value(int16_t);  /* 波特率档位 → 实际波特率数值 */
```

### 烧录器引脚角色标签

*📄 Source: [settings.c](../../src/app/settings/settings.c#L180-L189)*

```c
const char* settings_flasher_role_label(flasher_signal_t role);
```

返回烧录器信号角色的显示字符串：

| 角色 | 返回值 |
|------|--------|
| `FLASHER_SIG_NONE` | `"未分配"` |
| `FLASHER_SIG_TX` | `"TX"` |
| `FLASHER_SIG_RX` | `"RX"` |
| `FLASHER_SIG_BOOT` | `"BOOT/DTR"` |

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
| 横屏/竖屏 | `on_screen_rotation_change_cb()` | `M5.Display.setRotation()` |
| 波特率 | `on_serial_baud_change_cb()` | `Serial.end(); Serial.begin(baud)` |

> **为什么回调在 main.cpp 中**：这些回调需要调用 `M5.Display` 等 C++ Arduino API，而 main.cpp 是唯一的 C++ 入口文件。

---

> **See Also:** [App 初始化](app-init.md) | [屏幕旋转映射](../developer-guide.md)
