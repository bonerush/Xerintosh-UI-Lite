# 屏幕尺寸（HAL Screen）

> **Parent:** [知识地图](../index.md) | **Related:** [显示驱动](display.md)

## 概述

`hal_screen.h` / `src/hal/hal_screen.c` 提供跨平台的屏幕尺寸查询入口，避免布局模块直接依赖完整的 `hal_display.h`。

- **Native 测试环境**：使用编译期常量 `HAL_SCREEN_WIDTH` / `HAL_SCREEN_HEIGHT`
- **真机环境**：使用运行时变量 `g_screen_width` / `g_screen_height`，由 `hal_display_init()` 与 `hal_display_set_rotation()` 从 `M5.Display` 读取并更新

## API

*📄 Source: [hal_screen.h](../../src/hal/hal_screen.h#L19-L43)*

```c
#ifdef NATIVE_TEST
#define HAL_SCREEN_WIDTH  80
#define HAL_SCREEN_HEIGHT 160
#else
extern int16_t g_screen_width;
extern int16_t g_screen_height;
#define HAL_SCREEN_WIDTH  g_screen_width
#define HAL_SCREEN_HEIGHT g_screen_height
#endif

void hal_screen_get_size(int16_t *w, int16_t *h);
```

## 设计目的

1. **解耦**：`hal_layout.h` 等只关心屏幕尺寸的模块，无需包含完整的显示驱动头文件。
2. **运行时适配**：真机方向切换（横屏/竖屏）后，`g_screen_width` / `g_screen_height` 会自动更新，UI 布局无需硬编码。
3. **向后兼容**：`hal_display.h` 继续提供 `SCREEN_WIDTH` / `SCREEN_HEIGHT` 宏，现有调用方不受影响。

---

> **See Also:** [显示驱动](display.md) | [知识地图](../index.md)
