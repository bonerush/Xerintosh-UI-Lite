# 弹窗与信息栏绘制（UI Draw Widgets）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [绘制管线](drawer.md), [项目系统](item.md)

## 概述

`ui_draw_widgets` 实现 UI 框架的**弹窗（Pop-up）**和**信息栏（Info Bar）**绘制。两者都采用三层嵌套圆角矩形，营造"浮雕"立体边框效果。弹窗居中显示，信息栏在顶部显示。

---

## 信息栏

*📄 Source: [ui_draw_widgets.c](../../src/ui/ui_draw_widgets.c#L21-L67)*

信息栏是顶部的窄条通知区域，用于显示临时提示信息（如操作成功、错误提示）。

```c
void xerintosh_draw_info_bar()
{
  if (!g_xerintosh_info_bar.is_running) return;

  /* 到达目标位置后记录时间戳，用于判断是否超时 */
  if (g_xerintosh_info_bar.y_info_bar == g_xerintosh_info_bar.y_info_bar_trg)
    g_xerintosh_info_bar.time = hal_get_ticks();

  /* 超时后向屏幕上方移出 */
  if (g_xerintosh_info_bar.time - g_xerintosh_info_bar.time_start >= g_xerintosh_info_bar.span)
  {
    g_xerintosh_info_bar.y_info_bar_trg = 0 - 2 * INFO_BAR_HEIGHT;
    if (g_xerintosh_info_bar.y_info_bar == g_xerintosh_info_bar.y_info_bar_trg)
      g_xerintosh_info_bar.is_running = false;
  }

  int16_t _x_info_bar = SCREEN_WIDTH/2 - g_xerintosh_info_bar.w_info_bar/2;
  int16_t _y_info_bar_1 = g_xerintosh_info_bar.y_info_bar - 4;
  int16_t _y_info_bar_2 = g_xerintosh_info_bar.y_info_bar + INFO_BAR_HEIGHT;

  xerintosh_set_font(hal_get_cn_font());

  /* 第一层：阴影底色 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_info_bar + 3, _y_info_bar_1 + 3,
                  (int16_t)g_xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 4, g_xerintosh_draw_color);

  /* 第二层：外框 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_info_bar.w_info_bar + 4)/2), _y_info_bar_1,
                  (int16_t)(g_xerintosh_info_bar.w_info_bar + 4), INFO_BAR_HEIGHT + 6, 4, g_xerintosh_draw_color);

  /* 第三层：内框 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_info_bar, _y_info_bar_1,
                  (int16_t)g_xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 3, g_xerintosh_draw_color);

  /* 高光与文字 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(_x_info_bar + 2, _y_info_bar_2 - 2, (int16_t)(g_xerintosh_info_bar.w_info_bar - 4), g_xerintosh_draw_color);
  hal_draw_pixel(_x_info_bar + 1, _y_info_bar_2 - 3, g_xerintosh_draw_color);
  hal_draw_pixel(_x_info_bar - 2, _y_info_bar_2 - 3, g_xerintosh_draw_color);

  hal_draw_string(_x_info_bar + 6,
                 (int16_t)(g_xerintosh_info_bar.y_info_bar + hal_get_font_height() - 2),
                 g_xerintosh_info_bar.content, g_xerintosh_draw_color);
}
```

### 中文伪代码拆解

```
函数 绘制信息栏() {
    if (信息栏未运行) return

    // 第一步：到达目标位置后，开始计时
    if (信息栏Y == 信息栏目标Y) {
        信息栏.当前时间 = 获取Tick()
    }

    // 第二步：超时后自动收回
    if (当前时间 - 启动时间 >= 持续时间) {
        信息栏目标Y = 屏幕外上方
        if (已完全收回) {
            标记为停止运行
            return
        }
    }

    // 第三步：三层嵌套绘制，营造立体边框
    颜色 = 白
    绘制圆角实心矩形(偏移+1, y+3, ...)     // 最外层：白色阴影

    颜色 = 黑
    绘制圆角实心矩形(居中, y-2, ...)       // 中层：黑色边框

    颜色 = 白
    绘制圆角实心矩形(偏移-2, y, ...)       // 内层：白色主体

    // 第四步：绘制文字
    绘制字符串(居中X+3, 信息栏Y+字高+1, 信息栏内容)
}
```

**自动消失机制**：信息栏到达展开位置后记录当前时间，超过 `span` 毫秒后自动将目标位置设为屏幕外。`xerintosh_animation()` 负责平滑收回。

---

## 弹窗

*📄 Source: [ui_draw_widgets.c](../../src/ui/ui_draw_widgets.c#L75-L135)*

弹窗是居中的通知框，尺寸比信息栏大（`POP_UP_HEIGHT = 48` vs `INFO_BAR_HEIGHT = 15`），支持自动换行（最多 3 行），用于显示更重要的提示。

```c
void xerintosh_draw_pop_up()
{
  if (!g_xerintosh_pop_up.is_running) return;

  /* 到达目标位置后记录时间戳 */
  if (g_xerintosh_pop_up.y_pop_up == g_xerintosh_pop_up.y_pop_up_trg)
    g_xerintosh_pop_up.time = hal_get_ticks();

  /* 超时后向上移出 */
  if (g_xerintosh_pop_up.time - g_xerintosh_pop_up.time_start >= g_xerintosh_pop_up.span)
  {
    g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
    if (g_xerintosh_pop_up.y_pop_up == g_xerintosh_pop_up.y_pop_up_trg)
      g_xerintosh_pop_up.is_running = false;
  }

  /* 使用缓存高度；缓存无效时重算 */
  int16_t pop_h = g_xerintosh_pop_up.cached_pop_h;
  int16_t content_h = g_xerintosh_pop_up.cached_content_h;
  int16_t fh = hal_get_font_height();
  uint8_t n = g_xerintosh_pop_up.wrap_line_count;
  if (n < 1) n = 1;
  if (n > POP_UP_WRAP_LINES) n = POP_UP_WRAP_LINES;
  if (pop_h == 0) {
    pop_h = popup_compute_height(n);
    content_h = (int16_t)(n * fh + (n - 1) * 2);
  }

  int16_t _x_pop_up = SCREEN_WIDTH/2 - g_xerintosh_pop_up.w_pop_up/2;
  int16_t _y_pop_up = g_xerintosh_pop_up.y_pop_up + pop_h;

  xerintosh_set_font(hal_get_cn_font());

  /* 外框 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_pop_up.w_pop_up + 4)/2 - 2), (int16_t)(g_xerintosh_pop_up.y_pop_up - 2),
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 8), pop_h + 4, 5, g_xerintosh_draw_color);

  /* 内框 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_pop_up - 2, (int16_t)g_xerintosh_pop_up.y_pop_up,
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 4), pop_h, 3, g_xerintosh_draw_color);

  /* 高光与文字 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(_x_pop_up, _y_pop_up - 2, (int16_t)g_xerintosh_pop_up.w_pop_up, g_xerintosh_draw_color);
  hal_draw_pixel(_x_pop_up - 1, _y_pop_up - 3, g_xerintosh_draw_color);
  hal_draw_pixel((int16_t)(SCREEN_WIDTH/2 + g_xerintosh_pop_up.w_pop_up/2), _y_pop_up - 3, g_xerintosh_draw_color);

  /* 多行文字渲染 */
  {
    int16_t y_text = (int16_t)(g_xerintosh_pop_up.y_pop_up + (pop_h - content_h) / 2 + fh - 2);

    for (uint8_t i = 0; i < n; i++)
    {
      hal_draw_string(_x_pop_up + 3, y_text,
                      g_xerintosh_pop_up.wrap_lines[i], g_xerintosh_draw_color);
      y_text += fh + 2;
    }
  }
}
```

### 与信息栏的区别

| 特性 | 信息栏 | 弹窗 |
|------|--------|------|
| 默认位置 | 顶部（`y = 0`） | 居中（`y = (SCREEN_HEIGHT - pop_h) / 2`） |
| 高度 | `INFO_BAR_HEIGHT = 15` | 动态计算（最小 24，最大由内容决定） |
| 换行支持 | 单行 | 最多 `POP_UP_WRAP_LINES = 3` 行 |
| 用途 | 轻量提示（操作反馈） | 重要通知（错误、确认） |
| 显示时长 | 由调用方传入 `_span` | 由调用方传入 `_span` |

---

## 状态管理

弹窗和信息栏的状态由 `ui_widget.h` 中的结构体管理：

*📄 Source: [ui_widget.h](../../src/ui/ui_widget.h#L27-L35) / [ui_widget.h](../../src/ui/ui_widget.h#L74-L86)*

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
  int16_t cached_content_h;
} xerintosh_pop_up_t;
```

**关键区别**：
- `content` 是 `const char *` 指针（不是 `char[]` 数组），指向调用方传入的字符串，不由弹窗/信息栏内部管理内存
- 弹窗支持多行：通过 `wrap_lines[]` 数组存储换行后的各行指针，`wrap_line_count` 记录实际行数
- `cached_pop_h` / `cached_content_h` 在 `xerintosh_push_pop_up()` 时预计算，避免 `draw_pop_up()` 每帧重复执行浮点乘法和 `hal_get_font_height()`

---

## Widget 管理 API

*📄 Source: [ui_widget.h](../../src/ui/ui_widget.h#L39-L105) / [ui_item_popup.c](../../src/ui/ui_item_popup.c#L87-L273)*

信息栏和弹窗的生命周期由 `ui_item_popup.c` 管理，`ui_widget.h` 暴露以下 API：

```c
/* 推送顶部信息栏，_span 为显示持续时间（毫秒） */
void xerintosh_push_info_bar(const char *_content, const uint16_t _span);

/* 推送中部弹窗，支持自动换行（最多 3 行） */
void xerintosh_push_pop_up(const char *_content, const uint16_t _span);

/* 立即隐藏弹窗（无动画，瞬间移出屏幕） */
void xerintosh_hide_pop_up(void);

/* 动画退出弹窗（触发向上滑出动画，动画结束后自动停止） */
void xerintosh_dismiss_pop_up(void);
```

### 使用区别

| 函数 | 行为 | 适用场景 |
|---|---|---|
| `xerintosh_push_info_bar` | 从顶部展开信息栏，超时后自动收回 | 操作成功、状态提示 |
| `xerintosh_push_pop_up` | 居中弹出弹窗，自动按可用宽度折行 | 错误提示、重要通知 |
| `xerintosh_hide_pop_up` | 瞬间将弹窗移出屏幕并停止运行 | 需要立即清除弹窗 |
| `xerintosh_dismiss_pop_up` | 设置目标位置到屏幕外，由动画平滑收回 | 用户主动关闭 |

**注意**：`content` 是指向调用方字符串的指针，调用方需保证在显示期间字符串有效。弹窗换行缓冲区使用 `ui_item_popup.c` 内部的静态数组，不对原始内容做原地修改。

---

> **See Also:** [绘制管线](drawer.md) | [项目系统](item.md) | [核心引擎](core.md) | [Widget 数据模型](widget.md)
