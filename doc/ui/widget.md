# 控件数据模型（UI Widget）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [弹窗与信息栏绘制](draw-widgets.md), [项目系统](item.md)

## 概述

`ui_widget` 定义 Xerintosh 框架中的两种临时通知控件：**信息栏（Info Bar）**和**弹窗（Pop-up）**。它们都通过位置/宽度动画滑入滑出，由 `ui_item_popup.c` 管理生命周期。

---

## 信息栏

### 数据结构

*📄 Source: [ui_widget.h](../../src/ui/ui_widget.h#L27-L35)*

```c
typedef struct xerintosh_info_bar_t
{
  const char *content;       /* 显示文本 */
  uint16_t span;             /* 显示持续时间（毫秒） */
  float y_info_bar, y_info_bar_trg, w_info_bar, w_info_bar_trg;  /* 位置与宽度 */
  bool is_running;           /* 是否正在显示 */
  uint32_t time_start;       /* 开始显示的时间戳 */
  uint32_t time;             /* 最近一次更新的时间戳 */
} xerintosh_info_bar_t;
```

| 字段 | 说明 |
|---|---|
| `content` | 指向调用方传入的文本指针 |
| `span` | 显示持续时间（毫秒） |
| `y/w_info_bar` | 当前位置与宽度 |
| `*_trg` | 目标位置与宽度 |
| `is_running` | 是否仍在显示（即使已超时收回，动画未完成时仍为 true） |
| `time_start` | 开始显示时间 |
| `time` | 最近一次到达目标位置的时间，用于超时判断 |

### 推送

*📄 Source: [ui_item_popup.c](../../src/ui/ui_item_popup.c#L87-L105)*

```c
void xerintosh_push_info_bar(const char *_content, const uint16_t _span);
```

推送时设置 `y_info_bar_trg = 0` 并计算宽度。如果信息栏正在显示相同内容，则重置计时器。

---

## 弹窗

### 数据结构

*📄 Source: [ui_widget.h](../../src/ui/ui_widget.h#L74-L86)*

```c
typedef struct xerintosh_pop_up_t
{
  const char *content;       /* 显示文本 */
  uint16_t span;             /* 显示持续时间（毫秒） */
  float y_pop_up, y_pop_up_trg, w_pop_up, w_pop_up_trg;  /* 位置与宽度 */
  bool is_running;           /* 是否正在显示 */
  uint32_t time_start;       /* 开始显示的时间戳 */
  uint32_t time;             /* 最近一次更新的时间戳 */
  const char *wrap_lines[POP_UP_WRAP_LINES];  /* 换行后的各行指针 */
  uint8_t wrap_line_count;   /* 实际行数 */
  int16_t cached_pop_h;      /* 缓存弹窗高度（避免每帧重算） */
  int16_t cached_content_h;  /* 缓存内容区高度 */
} xerintosh_pop_up_t;
```

弹窗比信息栏多了多行支持：`wrap_lines[]` 存储断行后的各行指针，`wrap_line_count` 记录行数，`cached_pop_h` 和 `cached_content_h` 在推送时预计算。

### 推送

*📄 Source: [ui_item_popup.c](../../src/ui/ui_item_popup.c#L115-L253)*

```c
void xerintosh_push_pop_up(const char *_content, const uint16_t _span);
```

推送时会根据可用宽度自动换行（最多 `POP_UP_WRAP_LINES = 3` 行），并计算弹窗高度和宽度。

### 隐藏与退出

*📄 Source: [ui_item_popup.c](../../src/ui/ui_item_popup.c#L258-L273)*

```c
void xerintosh_hide_pop_up(void);    /* 立即隐藏，无动画 */
void xerintosh_dismiss_pop_up(void); /* 动画向上滑出 */
```

---

## 管理 API 总览

| 函数 | 作用 |
|---|---|
| `xerintosh_push_info_bar` | 推送顶部信息栏 |
| `xerintosh_push_pop_up` | 推送中部弹窗（支持自动换行） |
| `xerintosh_hide_pop_up` | 立即隐藏弹窗 |
| `xerintosh_dismiss_pop_up` | 动画退出弹窗 |

---

## 与其他组件的关系

- **ui_draw_widgets.c**：负责实际绘制信息栏和弹窗
- **ui_core.c**：`xerintosh_ui_widget_core()` 每帧刷新位置并触发绘制
- **ui_item.h**：通过向后兼容宏 `g_xerintosh_info_bar` / `g_xerintosh_pop_up` 暴露全局实例

---

> **See Also:** [弹窗与信息栏绘制](draw-widgets.md) | [核心引擎](core.md) | [项目系统](item.md)
