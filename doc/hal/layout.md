# 屏幕布局（HAL Layout）

> **Parent:** [HAL 层索引](index.md) | **Related:** [显示驱动](display.md), [屏幕尺寸](screen.md)

## 概述

`hal_layout.h` 提供 HAL 级布局原语，将 UI 定位从“硬编码像素坐标”提升为“语义化宏”。所有宏基于 `SCREEN_WIDTH` / `SCREEN_HEIGHT` / `hal_get_font_height()` 计算，C 兼容且零运行时开销。

---

## 三层宏体系

### 层1：原始边距常量

*📄 Source: [hal_layout.h](../../src/hal/hal_layout.h#L24-L28)*

```c
#define HAL_MARGIN_SM   ((int16_t)2)   /* 小边距：按钮 padding，分隔线间距 */
#define HAL_MARGIN_MD   ((int16_t)4)   /* 中边距：标准左右缩进 */
#define HAL_MARGIN_LG   ((int16_t)8)   /* 大边距：区块间间距 */
```

### 层2：语义化尺寸

*📄 Source: [hal_layout.h](../../src/hal/hal_layout.h#L32-L36)*

```c
#define HAL_ROW_H()     ((int16_t)(hal_get_font_height() + HAL_MARGIN_SM * 2))
```

`HAL_ROW_H()` 返回标准行高，用于 header、footer、列表行等需要固定高度的场景。

### 层3：区域边界与对齐

*📄 Source: [hal_layout.h](../../src/hal/hal_layout.h#L38-L81)*

```c
#define HAL_HEADER_TOP()    ((int16_t)0)
#define HAL_HEADER_BOTTOM() ((int16_t)HAL_ROW_H())
#define HAL_FOOTER_TOP()    ((int16_t)(SCREEN_HEIGHT - HAL_ROW_H()))
#define HAL_FOOTER_BOTTOM() ((int16_t)SCREEN_HEIGHT)

#define HAL_CENTER_X(w)     ((int16_t)((SCREEN_WIDTH - (w)) / 2))
#define HAL_CENTER_Y(h)     ((int16_t)((SCREEN_HEIGHT - (h)) / 2))
#define HAL_LEFT_X()        ((int16_t)HAL_MARGIN_MD)
#define HAL_TEXT_BASELINE(row_top)  ((int16_t)(row_top) + hal_get_font_height())
```

| 宏 | 含义 |
|---|---|
| `HAL_HEADER_TOP/BOTTOM` | Header 区域上下边界 |
| `HAL_FOOTER_TOP/BOTTOM` | Footer 区域上下边界 |
| `HAL_CENTER_X(w)` | 使宽度为 `w` 的元素水平居中 |
| `HAL_CENTER_Y(h)` | 使高度为 `h` 的元素垂直居中 |
| `HAL_LEFT_X()` | 左对齐 x 坐标（标准缩进） |
| `HAL_TEXT_BASELINE(row_top)` | 行顶部 y 对应的文字 baseline |

---

## 使用示例

```c
/* 在屏幕中央绘制一个宽度 60、高度 20 的按钮 */
int16_t x = HAL_CENTER_X(60);
int16_t y = HAL_CENTER_Y(20);
hal_draw_fill_rect(x, y, 60, 20, COLOR_FG);

/* 在顶部 Header 居中绘制标题 */
int16_t title_w = hal_get_string_width("Settings");
int16_t title_x = HAL_CENTER_X(title_w);
int16_t title_y = HAL_TEXT_BASELINE(HAL_HEADER_TOP());
hal_draw_string(title_x, title_y, "Settings", COLOR_BG);

/* 左侧列表项 */
hal_draw_string(HAL_LEFT_X(), HAL_HEADER_BOTTOM() + 4, "Item", COLOR_FG);
```

---

## 设计原则

- **零开销**：所有值通过 `#define` 在编译期展开，不引入函数调用。
- **方向无关**：基于 `SCREEN_WIDTH` / `SCREEN_HEIGHT`，横竖屏切换后无需修改调用代码。
- **字体感知**：`HAL_ROW_H()` 和 `HAL_TEXT_BASELINE()` 依赖 `hal_get_font_height()`，保证不同字体下的对齐一致。

---

> **See Also:** [显示驱动](display.md) | [屏幕尺寸](screen.md) | [UI 核心层](../ui/index.md)
