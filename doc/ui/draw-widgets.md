# 弹窗与信息栏绘制（UI Draw Widgets）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [绘制管线](drawer.md), [项目系统](item.md)

## 概述

`ui_draw_widgets` 实现 UI 框架的**弹窗（Pop-up）**和**信息栏（Info Bar）**绘制。两者都采用三层嵌套圆角矩形，营造"浮雕"立体边框效果。弹窗居中显示，信息栏在顶部显示。

---

## 信息栏

*📄 Source: [ui_draw_widgets.c](../../src/ui/ui_draw_widgets.c#L20-L72)*

信息栏是顶部的窄条通知区域，用于显示临时提示信息（如操作成功、错误提示）。

```c
void xerintosh_draw_info_bar()
{
  if (!xerintosh_info_bar.is_running) return;

  if (xerintosh_info_bar.y_info_bar == xerintosh_info_bar.y_info_bar_trg)
    xerintosh_info_bar.time = get_ticks();

  if (xerintosh_info_bar.time - xerintosh_info_bar.time_start >= xerintosh_info_bar.span) {
    xerintosh_info_bar.y_info_bar_trg = 0 - 2 * INFO_BAR_HEIGHT;
    if (xerintosh_info_bar.y_info_bar == xerintosh_info_bar.y_info_bar_trg)
      xerintosh_info_bar.is_running = false;
  }

  /* 三层嵌套圆角矩形 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x + 1, (int16_t)xerintosh_info_bar.y_info_bar + 3,
                           xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT, 2, ...);   // 外层阴影

  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (w + 4)/2 - 2), ...,
                           w + 4, INFO_BAR_HEIGHT + 4, 2, ...);                       // 中层底色

  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x - 2, (int16_t)xerintosh_info_bar.y_info_bar,
                           xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT, 2, ...);    // 内层主体

  hal_draw_string(_x + 3,
                  (int16_t)(xerintosh_info_bar.y_info_bar + hal_get_font_height() + 1),
                  xerintosh_info_bar.content, g_xerintosh_draw_color);
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

*📄 Source: [ui_draw_widgets.c](../../src/ui/ui_draw_widgets.c#L74-L126)*

弹窗是居中的通知框，尺寸比信息栏大（`POP_UP_HEIGHT = 20` vs `INFO_BAR_HEIGHT = 15`），用于显示更重要的提示。

```c
void xerintosh_draw_pop_up()
{
  if (!xerintosh_pop_up.is_running) return;

  if (xerintosh_pop_up.y_pop_up == xerintosh_pop_up.y_pop_up_trg)
    xerintosh_pop_up.time = get_ticks();

  if (xerintosh_pop_up.time - xerintosh_pop_up.time_start >= xerintosh_pop_up.span) {
    xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
    if (xerintosh_pop_up.y_pop_up == xerintosh_pop_up.y_pop_up_trg)
      xerintosh_pop_up.is_running = false;
  }

  /* 三层嵌套圆角矩形 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_pop_up + 1, (int16_t)xerintosh_pop_up.y_pop_up + 3, ...);   // 外层阴影

  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (w + 4)/2 - 2), ...);                 // 中层底色

  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_pop_up - 2, (int16_t)xerintosh_pop_up.y_pop_up, ...);        // 内层主体

  hal_draw_string(_x_pop_up + 3,
                  (int16_t)(xerintosh_pop_up.y_pop_up + hal_get_font_height() + 1),
                  xerintosh_pop_up.content, g_xerintosh_draw_color);
}
```

### 与信息栏的区别

| 特性 | 信息栏 | 弹窗 |
|------|--------|------|
| 默认位置 | 顶部（`y = 0`） | 居中偏上（`y = SCREEN_HEIGHT/3`） |
| 高度 | `INFO_BAR_HEIGHT = 15` | `POP_UP_HEIGHT = 20` |
| 用途 | 轻量提示（操作反馈） | 重要通知（错误、确认） |
| 显示时长 | 默认 1500ms | 默认 2000ms |

---

## 状态管理

弹窗和信息栏的状态由 `ui_item.h` 中的结构体管理：

```c
typedef struct {
    float y_pop_up, y_pop_up_trg;
    float w_pop_up, w_pop_up_trg;
    uint32_t time;
    uint32_t time_start;
    uint32_t span;
    bool is_running;
    char content[24];
} xerintosh_pop_up_t;
```

---

> **See Also:** [绘制管线](drawer.md) | [项目系统](item.md) | [核心引擎](core.md)
