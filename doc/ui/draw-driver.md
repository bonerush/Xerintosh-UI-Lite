# 绘制驱动适配（UI Draw Driver）

> **Parent:** [知识地图](../index.md) | **Related:** [项目系统](item.md), [绘制管线](drawer.md), [显示驱动](../hal/display.md)

## 概述

`ui_draw_driver` 是一个**宏桥接层**。原始 OLED 框架使用 `oled_*` 开头的函数（如 `oled_draw_box`、`oled_draw_UTF8`），而 TFT 移植版使用 `hal_*` 开头的 HAL API。本层通过宏定义将旧 API 映射到新 API，使 UI 核心代码几乎无需修改即可运行。

---

## 关键概念

### 宏映射表

*📄 Source: [ui_draw_driver.h](../../src/ui/ui_draw_driver.h#L11-L35)*

```c
#define get_ticks() hal_get_ticks()
#define delay(ms) hal_delay_ms(ms)

#define oled_set_font(font) hal_set_font(font)
#define oled_draw_str(x, y, str) hal_draw_string(x, y, str, COLOR_FG)
#define oled_draw_UTF8(x, y, str) hal_draw_utf8(x, y, str, COLOR_FG)
#define oled_get_str_width(str) hal_get_string_width(str)
#define oled_get_UTF8_width(str) hal_get_utf8_width(str)
#define oled_get_str_height() hal_get_font_height()

#define oled_draw_pixel(x, y) hal_draw_pixel(x, y, COLOR_FG)
#define oled_draw_circle(x, y, r) hal_draw_circle(x, y, r, COLOR_FG)
#define oled_draw_R_box(x, y, w, h, r) hal_draw_fill_round_rect(x, y, w, h, r, COLOR_FG)
#define oled_draw_box(x, y, w, h) hal_draw_fill_rect(x, y, w, h, COLOR_FG)
#define oled_draw_frame(x, y, w, h) hal_draw_rect(x, y, w, h, COLOR_FG)
#define oled_draw_R_frame(x, y, w, h, r) hal_draw_round_rect(x, y, w, h, r, COLOR_FG)
#define oled_draw_H_line(x, y, l) hal_draw_h_line(x, y, l, COLOR_FG)
#define oled_draw_V_line(x, y, h) hal_draw_v_line(x, y, h, COLOR_FG)
#define oled_draw_line(x1, y1, x2, y2) hal_draw_line(x1, y1, x2, y2, COLOR_FG)
```

#### 中文伪代码拆解

```
// 原始 OLED API          →    新的 TFT HAL API
获取Tick()                →    hal_获取Tick()
延时(毫秒)                →    hal_延时毫秒()

设置字体(字体)            →    hal_设置字体(字体)
绘制字符串(x,y,文本)      →    hal_绘制字符串(x,y,文本,前景色)
绘制UTF8(x,y,文本)        →    hal_绘制UTF8(x,y,文本,前景色)
获取字符串宽度(文本)      →    hal_获取字符串宽度(文本)
获取UTF8宽度(文本)        →    hal_获取UTF8宽度(文本)
获取字体高度()            →    hal_获取字体高度()

绘制像素(x,y)             →    hal_绘制像素(x,y,前景色)
绘制实心矩形(x,y,w,h)     →    hal_填充矩形(x,y,w,h,前景色)
绘制圆角实心矩形(...)     →    hal_填充圆角矩形(...,前景色)
// ... 以此类推
```

**核心思想**：OLED 使用“设置画笔颜色 → 绘制”的状态机模式；TFT HAL 使用“每次绘制都带颜色参数”的函数式模式。宏桥接层把旧的状态机调用自动填充 `COLOR_FG`（白色），实现无感迁移。

### 已废弃的 OLED 专属 API

*📄 Source: [ui_draw_driver.h](../../src/ui/ui_draw_driver.h#L38-L46)*

```c
#define oled_draw_H_dotted_line(x, y, l) /* placeholder */
#define oled_draw_V_dotted_line(x, y, h) /* placeholder */
#define oled_set_draw_color(color) /* color handled by HAL */
#define oled_set_font_mode(mode) /* TFT doesn't need */
#define oled_set_font_direction(dir) /* TFT doesn't need */
#define oled_clear_buffer() hal_display_clear()
#define oled_send_buffer() hal_display_flush()
#define oled_send_area_buffer(x, y, w, h) /* not needed */
```

这些宏定义为空实现或映射到 HAL 的等价操作：
- `oled_set_draw_color`：TFT 没有全局画笔颜色概念，每次绘制自带颜色参数
- `oled_set_font_mode` / `oled_set_font_direction`：M5GFX 不需要这些 OLED 专属字体模式
- `oled_clear_buffer` / `oled_send_buffer`：映射到双缓冲的 `clear` / `flush`

### 驱动初始化

*📄 Source: [ui_draw_driver.c](../../src/ui/ui_draw_driver.c)*

```c
void xerintosh_ui_driver_init(void) {
    hal_display_init();
    hal_system_init();
    hal_input_init();
}
```

`xerintosh_ui_driver_init()` 是 UI 层对外暴露的统一初始化入口，内部按顺序初始化显示、系统时钟和输入三个 HAL 子系统。

---

## 与其他组件的关系

- **所有 UI `.c` 文件**：通过 `#include "ui_draw_driver.h"` 隐式使用宏映射
- **hal_display**：宏的目标端，所有 `oled_draw_*` 最终落入 `hal_draw_*`
- **main.cpp**：`setup()` 中调用 `xerintosh_ui_driver_init()`

---

> **See Also:** [显示驱动](../hal/display.md) | [项目系统](item.md) | [绘制管线](drawer.md)
