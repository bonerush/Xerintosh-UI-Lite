# 绘制驱动适配（UI Draw Driver）⚠️ 已移除

> **Parent:** [UI 核心层索引](index.md) | **Related:** [显示驱动](../hal/display.md)

## ⚠️ 此模块已在 UI 重构（Task 4）中移除

`src/ui/ui_draw_driver.c` 和 `src/ui/ui_draw_driver.h` 两个文件已于 2026 年 6 月的 UI 框架深度重构中删除。

### 移除原因

1. **薄包装层无存在价值**：`xerintosh_ui_driver_init()` 仅包含三行调用（`hal_display_init()`、`hal_system_init()`、`hal_input_init()`），没有提供任何额外的抽象或逻辑。调用方直接使用 HAL API 即可。
2. **减少文件数量**：遵循"多个小文件 > 少量大文件"原则，但排除价值为零的文件。此模块属于死代码（dead code）。
3. **减少包含链**：移除后，`#include "ui_draw_driver.h"` 被替换为直接 `#include "hal/hal_display.h"` 等，减少了不必要的间接包含。

### 迁移指南

**重构前**（使用 `ui_draw_driver.h`）：
```c
#include "ui_draw_driver.h"

void setup() {
    xerintosh_ui_driver_init();  // 已删除
}
```

**重构后**（直接使用 HAL）：
```c
#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include "hal/hal_input.h"

void setup() {
    hal_display_init();
    hal_system_init();
    hal_input_init();
}
```

所有 `#include "ui_draw_driver.h"` 的引用已在代码库中清理完毕，替换为直接包含 `hal/hal_display.h` 或具体需要的 HAL 头文件。

---

## 历史参考（归档内容）

以下内容仅供历史参考，**当前代码已不再使用**。

### 原宏映射表

原始 OLED 框架使用 `oled_*` 开头的函数，移植到 TFT（M5GFX）时通过宏桥接层映射到 `hal_*` API：

| 原始 API | 映射到 | 说明 |
|----------|--------|------|
| `oled_set_font(f)` | `hal_set_font(f)` | 设置字体 |
| `oled_draw_UTF8(x,y,s)` | `hal_draw_utf8(x,y,s, COLOR_FG)` | 绘制 UTF-8 文本 |
| `oled_get_UTF8_width(s)` | `hal_get_utf8_width(s)` | 获取 UTF-8 文本宽度 |
| `oled_get_str_height()` | `hal_get_font_height()` | 获取当前字体高度 |
| `oled_draw_box(x,y,w,h)` | `hal_draw_fill_rect(x,y,w,h, COLOR_FG)` | 实心矩形 |
| `oled_draw_pixel(x,y)` | `hal_draw_pixel(x,y, COLOR_FG)` | 单像素 |
| `oled_draw_H_line(x,y,l)` | `hal_draw_h_line(x,y,l, COLOR_FG)` | 水平线 |
| `oled_clear_buffer()` | `hal_display_clear()` | 清除缓冲区 |
| `oled_send_buffer()` | `hal_display_flush()` | 刷新到屏幕 |

> **注意**：这些宏映射在代码中仍有使用，但定义已迁移到调用方或各自的头文件中，不再集中在一个"驱动适配"文件。

### 原驱动初始化

```c
void xerintosh_ui_driver_init(void) {
    hal_display_init();
    hal_system_init();
    hal_input_init();
}
```

此函数之所以被移除，是因为它是纯粹的"转发"——没有任何参数处理、错误检查或状态管理。调用方直接写三行 HAL 调用比通过一个包装函数更清晰。

---

> **See Also:** [显示驱动](../hal/display.md) | [核心引擎](core.md) | [全局上下文](context.md)
