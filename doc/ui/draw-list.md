# 列表绘制（UI Draw List）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [绘制管线](drawer.md), [项目系统](item.md), [核心引擎](core.md)

## 概述

`ui_draw_list` 是 UI 框架的**列表渲染实现**，负责绘制列表外观（边框、滚动条、装饰像素）、各类列表项（图标、开关、滑条）、选择器 XOR 高亮、滑块数值覆盖层、长按提示条以及文字跑马灯滚动效果。

---

## 可见性裁剪

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L24-L27)*

```c
static bool is_item_visible(int16_t _y_item)
{
  return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}
```

**核心思想**：在绘制每个列表项前检查其 Y 坐标是否在屏幕有效范围内（`LIST_INFO_BAR_HEIGHT = 3` 到 `SCREEN_HEIGHT = 160`），避免越界绘制和无效渲染。

---

## 列表外观

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L51-L114)*

列表外观包括顶部装饰横线、右上角消散像素簇、右侧滚动条及滑块：

```c
static void xerintosh_draw_list_appearance(int16_t selected_index, int16_t child_num)
{
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_h_line(0, 1, 66, g_xerintosh_draw_color);
  hal_draw_h_line(0, 0, 67, g_xerintosh_draw_color);

  /* 顶部右侧装饰像素 */
  const struct
  {
    uint8_t _start;
    uint8_t _end;
    uint8_t _step;
    uint8_t _y;
  } draw_cfg[] = {
      {67, 99, 2, 1},
      {68, 100, 2, 0},
      {102, 111, 3, 1},
      {103, 112, 3, 0},
      {115, 124, 5, 1},
      {116, 124, 5, 0}
    };

  for (uint8_t j = 0; j < sizeof(draw_cfg) / sizeof(draw_cfg[0]); ++j)
    for (uint8_t i = draw_cfg[j]._start; i <= draw_cfg[j]._end; i += draw_cfg[j]._step)
      hal_draw_pixel(i, draw_cfg[j]._y, g_xerintosh_draw_color);

  /* 右侧边框竖线 */
  hal_draw_v_line(SCREEN_WIDTH - 5, 0, SCREEN_HEIGHT, g_xerintosh_draw_color);
  hal_draw_v_line(SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT, g_xerintosh_draw_color);

  /* 滚动条 */
  static uint8_t _cached_child_num = 0;
  static int16_t _cached_screen_h = -1;
  static float _cached_length = 0;
  if (_cached_child_num != child_num || _cached_screen_h != SCREEN_HEIGHT) {
    _cached_child_num = child_num;
    _cached_screen_h = SCREEN_HEIGHT;
    _cached_length = ceilf((SCREEN_HEIGHT - 10.0f) / (float)_cached_child_num);
  }
  float _length_each_part = _cached_length;
  hal_draw_fill_rect(SCREEN_WIDTH - 4, 5 + selected_index * _length_each_part, 3, _length_each_part, g_xerintosh_draw_color);

  /* 滚动条内部高光线 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(SCREEN_WIDTH - 4, _length_each_part + (float)selected_index * _length_each_part, 3, g_xerintosh_draw_color);

  if (_length_each_part >= 9)
  {
    hal_draw_h_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part - 2.0f + (float)selected_index * _length_each_part), 3, g_xerintosh_draw_color);
    hal_draw_h_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part + 2.0f + (float)selected_index * _length_each_part), 3, g_xerintosh_draw_color);
  }

  /* 滚动条上下端点 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_rect(SCREEN_WIDTH - 4, 0, 3, 4, g_xerintosh_draw_color);
  hal_draw_fill_rect(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, 3, 4, g_xerintosh_draw_color);
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(SCREEN_WIDTH - 4, 2, 3, g_xerintosh_draw_color);
  hal_draw_pixel(SCREEN_WIDTH - 3, 1, g_xerintosh_draw_color);
  hal_draw_h_line(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 3, 3, g_xerintosh_draw_color);
  hal_draw_pixel(SCREEN_WIDTH - 3, SCREEN_HEIGHT - 2, g_xerintosh_draw_color);
}
```

### 中文伪代码拆解

```
函数 绘制列表外观() {
    设置绘制颜色为前景色(白)

    // 顶部两条水平装饰线
    绘制水平线(0, 1, 66)
    绘制水平线(0, 0, 67)

    // 右上角像素簇（营造"消散"视觉效果）
    配置数组 = { 6组递减密度的像素列 }
    for (每组配置) {
        for (x从起始到结束，按步进跳跃) {
            绘制像素(x, 配置的y)
        }
    }

    // 右侧边框竖线
    绘制竖线(屏幕宽-5, 0, 屏幕高)
    绘制竖线(屏幕宽-1, 0, 屏幕高)

    // 滚动条滑块
    每份高度 = 向上取整((屏幕高 - 10) / 当前菜单子项数)
    滑块Y = 5 + 选中索引 × 每份高度
    绘制实心矩形(屏幕宽-4, 滑块Y, 宽3, 高=每份高度)

    // 滑块内部高光线和端点装饰...
}
```

---

## 列表项绘制

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L121-L200)*

```c
static void xerintosh_draw_list_item()
{
  xerintosh_set_font(hal_get_cn_font());
  int16_t _font_h = hal_get_font_height();
  int16_t _font_h_2 = _font_h / 2;

  for (unsigned char i = 0; i < g_xerintosh_selector.selected_item->parent->child_num; i++)
  {
    xerintosh_list_item_t *_item = g_xerintosh_selector.selected_item->parent->child_list_item[i];
    int16_t _x_list_item = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
    int16_t _y_list_item = _item->y_list_item + g_xerintosh_camera.y_camera - _font_h_2;

    /* 根据类型分发到对应的绘制函数 */
    g_xerintosh_draw_color = COLOR_FG;
    xerintosh_dispatch_draw(_item, _x_list_item, _y_list_item);

    /* 自定义位图图标补充绘制 */
    if (_item->icon == custom_icon && _item->bitmap_data != NULL) {
      xerintosh_draw_item_bitmap(_item, _x_list_item, _y_list_item);
    }

    /* ═══ 文字绘制与滚动效果 ═══ */
    if (_y_list_item + _font_h_2 > LIST_INFO_BAR_HEIGHT &&
        _y_list_item + _font_h_2 < SCREEN_HEIGHT)
    {
      int16_t _text_width = hal_get_string_width(_item->content);
      bool _has_right_control = xerintosh_dispatch_has_right_control(_item);
      int16_t _right_margin = _has_right_control ? LIST_ITEM_RIGHT_MARGIN : 4;
      int16_t _avail_width = SCREEN_WIDTH - LIST_ITEM_LEFT_MARGIN - 10 - _right_margin;

      /* switch/slider 额外占用右侧控件空间 */
      if (_has_right_control)
        _avail_width -= 11;

      /* 防御性钳位：可用宽度不得为负，避免传给 HAL 裁剪函数行为未定义 */
      if (_avail_width < 1)
        _avail_width = 1;

      bool _is_selected = (_item == g_xerintosh_selector.selected_item);
      float _scroll_x = 0.0f;

      /* 计算文字循环滚动偏移 */
      if (_is_selected && _text_width > _avail_width) {
        if (!_item->is_scrolling) {
          _item->is_scrolling = true;
          _item->scroll_start_time = hal_get_ticks();
        }
        uint32_t _elapsed = hal_get_ticks() - _item->scroll_start_time;
        _scroll_x = xerintosh_compute_scroll_offset(_text_width, _avail_width, true, _elapsed);

        /* 文字滚动期间每帧都需清屏重绘，否则旧像素残留造成残影 */
        xerintosh_invalidate();
      } else {
        _item->is_scrolling = false;
      }

      /* 设置裁剪区域：限制文字只在 icon 右侧到控件左侧之间显示 */
      int16_t _clip_x = LIST_ITEM_LEFT_MARGIN + 10;
      int16_t _clip_y = _y_list_item - _font_h_2 - 2;
      int16_t _clip_h = _font_h + 4;
      hal_set_clip_rect(_clip_x, _clip_y, _avail_width, _clip_h);

      int16_t _cycle_dist = _text_width + _avail_width;
      int16_t _draw_x = _clip_x - (int16_t)_scroll_x;

      /* 绘制两份相同文字，形成无缝循环跑马灯 */
      hal_draw_string(_draw_x,
                     _y_list_item + _font_h_2,
                     _item->content, g_xerintosh_draw_color);
      hal_draw_string(_draw_x + _cycle_dist,
                     _y_list_item + _font_h_2,
                     _item->content, g_xerintosh_draw_color);

      hal_clear_clip_rect();
      /* ═══════════════════════ */
    }
  }

  g_xerintosh_refresh_list_value = false;
}
```

### 中文伪代码拆解

```
函数 绘制列表项() {
    设置字体(中文字体)
    字体高度 = hal_get_font_height()
    字体半高 = 字体高度 / 2

    for (遍历当前菜单的所有子项) {
        项 = 选择器.选中项.父项.子项数组[i]
        屏幕X = 相机X + 左边距4
        屏幕Y = 项.当前Y + 相机Y - 字体半高

        // 根据类型分发绘制
        switch (项的类型) {
            case 普通列表项/按钮/用户页: 仅绘制图标
            case 开关项: 绘制图标 + 右侧开关控件
            case 滑条项: 绘制图标 + 右侧数值
            default: 仅绘制图标
        }

        // 自定义位图
        if (项有自定义位图) 绘制位图图标

        // 文字绘制（含跑马灯滚动）
        if (文本在可视区内) {
            计算可用宽度（右侧控件会占用空间）
            // switch/slider 额外再减11px控件宽度

            if (当前项被选中 且 文本宽度 > 可用宽度) {
                // 启动/继续跑马灯滚动
                if (首次滚动) 记录开始时间
                滚动偏移 = 计算滚动偏移(文本宽, 可用宽, 已持续时间)
            } else {
                停止滚动
            }

            // 设置裁剪区域，限制文字只在有效范围显示
            设置裁剪矩形(icon右侧, 项Y, 可用宽, 字高+4)

            循环距离 = 文本宽 + 可用宽
            绘制X = 裁剪X - 滚动偏移

            // 绘制两份文字形成无缝循环
            绘制文字(绘制X, ...)
            绘制文字(绘制X + 循环距离, ...)

            清除裁剪区域
        }
    }

    清除列表值刷新标记 = false
}
```

**关键设计**：
- **裁剪区域**：文字只在图标右侧到控件左侧之间显示，不会溢出到滚动条区域
- **双份绘制**：通过绘制两份相同文字并在裁剪区外循环，实现无缝跑马灯效果
- **右侧控件空间预留**：switch/slider 会自动减少文字的可用宽度，避免与控件重叠

---

## 选择器高亮

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L207-L229)*

```c
void xerintosh_draw_selector()
{
  int16_t _x = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
  int16_t _y = g_xerintosh_selector.y_selector + g_xerintosh_camera.y_camera;

  /* XOR 反色矩形 */
  hal_draw_xor_rect(_x, _y, g_xerintosh_selector.w_selector, g_xerintosh_selector.h_selector);

  /* 右侧虚线装饰 */
  g_xerintosh_draw_color = COLOR_FG;
  for (int16_t i = g_xerintosh_selector.w_selector + _x;
       i <= g_xerintosh_selector.w_selector + _x + 7; i += 2)
  {
    for (int16_t j = _y; j <= _y + g_xerintosh_selector.h_selector - 1; j++)
    {
      if (j % 2 == 0) hal_draw_pixel(i + 1, j, g_xerintosh_draw_color);
      if (j % 2 == 1) hal_draw_pixel(i, j, g_xerintosh_draw_color);
    }
  }
}
```

---

## 滑块数值覆盖层

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L33-L43)*

已确认的滑块项（处于编辑状态）在选择器 XOR 反色之后绘制。实际实现通过 `xerintosh_dispatch_draw_overlay()` 派发表路由，不再内联遍历和类型检查：

```c
static void xerintosh_draw_slider_overlays(void)
{
  xerintosh_list_item_t *sel = g_xerintosh_selector.selected_item;
  if (sel == NULL || sel->parent == NULL) return;

  xerintosh_list_item_t *parent = sel->parent;
  for (uint8_t i = 0; i < parent->child_num; i++)
  {
    xerintosh_dispatch_draw_overlay(parent->child_list_item[i]);
  }
}
```

各类型的覆盖层绘制逻辑由 `ui_dispatch.c` 的派发表中的 `draw_overlay` 函数指针处理。slider_item 的覆盖层使用反色圆角矩形作为背景，避免 XOR 反色导致数值不可读。

---

## 文字滚动偏移计算

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L277-L290)*

```c
float xerintosh_compute_scroll_offset(int16_t text_width, int16_t avail_width,
                                   bool is_selected, uint32_t elapsed_ms)
{
  if (!is_selected || text_width <= avail_width)
    return 0.0f;

  int16_t cycle_distance = text_width + avail_width;
  if (cycle_distance <= 0)
    return 0.0f;

  const uint32_t SCROLL_CYCLE_MS = 3000;
  uint32_t phase = elapsed_ms % SCROLL_CYCLE_MS;
  return ((float)phase / (float)SCROLL_CYCLE_MS) * (float)cycle_distance;
}
```

**纯函数设计**：不依赖全局状态，只根据输入参数计算偏移量。这使其可以直接在单元测试中验证。

---

## 长按提示

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L238-L251)*

```c
void xerintosh_draw_long_press_hint(uint32_t duration_ms, uint32_t threshold_ms)
{
    if (duration_ms == 0 || duration_ms >= threshold_ms) return;

    float progress = (float)duration_ms / (float)threshold_ms;
    uint16_t bar_width = 40;
    uint16_t bar_height = 4;
    uint16_t x = SCREEN_WIDTH - bar_width - 6;
    uint16_t y = SCREEN_HEIGHT - 6;

    g_xerintosh_draw_color = COLOR_FG;
    hal_draw_rect(x, y, bar_width, bar_height, g_xerintosh_draw_color);
    hal_draw_fill_rect(x + 1, y + 1, (uint16_t)((bar_width - 2) * progress),
                       bar_height - 2, g_xerintosh_draw_color);
}
```

---

> **See Also:** [绘制管线](drawer.md) | [项目系统](item.md) | [核心引擎](core.md) | [行列表动画工具](ui-anim-row.md)
