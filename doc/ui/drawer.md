# 绘制管线（UI Drawer）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [核心引擎](core.md), [项目系统](item.md), [全局上下文](context.md)

## 概述

`ui_drawer` 是 UI 框架的**渲染层**，负责把数据模型（`ui_item` 中的列表项、选择器、弹窗等）转换为实际的像素绘制指令。所有绘制通过 `hal_*` API 输出到 TFT 后台缓冲区。

在 UI 重构中，此模块有两项关键改进：
1. **`xerintosh_is_item_visible()` 公开化**：原 `static` 可见性判断提升为公共 API
2. **滚动条长度缓存**：避免每帧重复计算 `ceilf()` 除法

---

## 关键概念

### 列表外观（滚动条 + 装饰线）

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L130-L186)*

```c
void xerintosh_draw_list_appearance()
{
  hal_draw_h_line(0, 1, 66, g_xerintosh_draw_color);
  hal_draw_h_line(0, 0, 67, g_xerintosh_draw_color);

  // 右上角装饰像素簇
  const struct {
    uint8_t _start, _end, _step, _y;
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

  // 右侧滚动条背景
  hal_draw_v_line(SCREEN_WIDTH - 5, 0, SCREEN_HEIGHT, g_xerintosh_draw_color);
  hal_draw_v_line(SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT, g_xerintosh_draw_color);

  // 滚动条滑块（含缓存优化）
  static uint8_t _cached_child_num = 0;
  static float _cached_length = 0;
  if (_cached_child_num != g_xerintosh_selector.selected_item->parent->child_num) {
    _cached_child_num = g_xerintosh_selector.selected_item->parent->child_num;
    _cached_length = ceilf((SCREEN_HEIGHT - 10.0f) / (float)_cached_child_num);
  }
  float _length_each_part = _cached_length;
  hal_draw_fill_rect(SCREEN_WIDTH - 4, 5 + g_xerintosh_selector.selected_index * _length_each_part, 3, _length_each_part, g_xerintosh_draw_color);
  // ... 滑块上的装饰分割线
}
```

#### 中文伪代码拆解

```
函数 绘制列表外观() {
    设置绘制颜色为前景色(白)

    // 顶部两条水平装饰线
    绘制水平线(0, 1, 66)
    绘制水平线(0, 0, 67)

    // 右上角像素簇（营造"消散"视觉效果）
    配置数组 = {
        {起始67, 结束99, 步进2, y=1},
        {起始68, 结束100, 步进2, y=0},
        // ... 6组递减密度的像素列
    }
    for (每组配置) {
        for (x从起始到结束，按步进跳跃) {
            绘制像素(x, 配置的y)
        }
    }

    // 右侧滚动条
    绘制竖线(屏幕宽-5, 0, 屏幕高)    // 左边界
    绘制竖线(屏幕宽-1, 0, 屏幕高)    // 右边界

    // 滑块高度 = 可视区 / 子项数
    每份高度 = 向上取整((屏幕高 - 10) / 当前菜单子项数)
    滑块Y = 5 + 选中索引 × 每份高度
    绘制实心矩形(屏幕宽-4, 滑块Y, 宽3, 高=每份高度)
}
```

### 列表项绘制

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L198-L288)*

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

    g_xerintosh_draw_color = COLOR_FG;
    xerintosh_dispatch_draw(_item, _x_list_item, _y_list_item);

    if (_item->icon == custom_icon && _item->bitmap_data != NULL) {
      xerintosh_draw_item_bitmap(_item, _x_list_item, _y_list_item);
    }

    /* 文字绘制与滚动效果... */
  }

  g_xerintosh_refresh_list_value = false;
}
```

#### 中文伪代码拆解

```
函数 绘制列表项() {
    设置字体(中文字体)
    字体半高 = hal_get_font_height() / 2

    for (遍历当前菜单的所有子项) {
        项 = 选择器.选中项.父项.子项数组[i]
        屏幕X = 相机X + 左边距4
        屏幕Y = 项.当前Y + 相机Y - 字体半高

        // 绘制行为由 ui_dispatch.c 的 vtable 按类型路由
        xerintosh_dispatch_draw(项, 屏幕X, 屏幕Y)

        if (项有自定义位图) 绘制位图图标

        // 文字绘制（含跑马灯滚动，详见 draw-list.md）
    }

    清除列表值刷新标记
}
```

**裁剪逻辑**：所有项通过 `xerintosh_is_item_visible()` 判断是否处于 `LIST_INFO_BAR_HEIGHT = 3` 到 `SCREEN_HEIGHT`（竖屏 160 / 横屏 80）的有效范围内，避免越界和无效绘制。

**滚动条长度缓存**：`_cached_child_num` 记录了上次计算时的 `child_num`。如果子项数量未改变（绝大多数帧），跳过 `ceilf()` 除法运算。`ceilf()` 是浮点运算中较昂贵的操作，在 ESP32 上每帧避免一次除法可节省几十微秒。

### 列表项可见性判断（公开 API）

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L24-L27)*

```c
bool xerintosh_is_item_visible(int16_t _y_item)
{
    return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}
```

原为 `static bool is_item_visible()`，重构中提取为公开 `xerintosh_is_item_visible()`，声明在 `ui_types.h`。在以下场景中被调用：
- `draw_list_item_*` 系列函数 — 跳过屏幕外项的绘制
- `xerintosh_draw_slider_overlays()` — 跳过屏幕外滑条编辑框的绘制
- 测试代码 — 验证边界条件下的可见性判断

### 选择器高亮（XOR 反色）

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L295-L317)*

```c
static void xerintosh_draw_selector()
{
  int16_t _x_selector = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
  int16_t _y_selector = g_xerintosh_selector.y_selector + g_xerintosh_camera.y_camera;

  /* XOR 反色矩形 */
  hal_draw_xor_rect(_x_selector, _y_selector, g_xerintosh_selector.w_selector, g_xerintosh_selector.h_selector);

  /* 右侧虚线装饰 */
  g_xerintosh_draw_color = COLOR_FG;
  for (int16_t i = g_xerintosh_selector.w_selector + _x_selector;
       i <= g_xerintosh_selector.w_selector + _x_selector + 7; i += 2)
  {
    for (int16_t j = _y_selector;
         j <= _y_selector + g_xerintosh_selector.h_selector - 1; j++)
    {
      if (j % 2 == 0)
        hal_draw_pixel(i + 1, j, g_xerintosh_draw_color);
      if (j % 2 == 1)
        hal_draw_pixel(i, j, g_xerintosh_draw_color);
    }
  }
}
```

#### 中文伪代码拆解

```
函数 绘制选择器() {
    屏幕X = 相机X + 左边距
    屏幕Y = 选择器当前Y + 相机Y

    // 第一步：XOR 反色矩形（核心高亮效果）
    hal_绘制XOR反色矩形(屏幕X, 屏幕Y, 选择器宽, 选择器高)
    // 效果：矩形区域内的所有像素颜色取反

    // 第二步：右侧虚线装饰（"选中标记"）
    for (x从选择器右边缘开始，向右跳2像素，共8像素) {
        for (y从选择器顶部到底部) {
            // 棋盘格图案
            if (y是偶数行) 绘制像素(x+1, y)
            if (y是奇数行) 绘制像素(x, y)
        }
    }
}
```

**关键区别**：原始 OLED 代码使用 `oled_set_draw_color(2)`（u8g2 专属反色模式），TFT 移植版改用 `hal_draw_xor_rect()`，通过像素级读取-异或-回写实现等价效果。

### 弹窗（Pop-up）

*📄 Source: [ui_draw_widgets.c](../../src/ui/ui_draw_widgets.c#L92-L157)*

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

  /* 优先使用缓存高度，避免每帧重算 */
  int16_t pop_h = g_xerintosh_pop_up.cached_pop_h;
  int16_t content_h = g_xerintosh_pop_up.cached_content_h;
  if (pop_h == 0) {
    pop_h = popup_compute_height(g_xerintosh_pop_up.wrap_line_count);
    /* ... fallback 重算 ... */
  }

  int16_t _x_pop_up = SCREEN_WIDTH/2 - g_xerintosh_pop_up.w_pop_up/2;
  int16_t _y_pop_up = g_xerintosh_pop_up.y_pop_up + pop_h;

  xerintosh_set_font(hal_get_cn_font());

  /* 外框 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_pop_up.w_pop_up + 4)/2 - 2),
                  (int16_t)(g_xerintosh_pop_up.y_pop_up - 2),
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 8), pop_h + 4, 5, g_xerintosh_draw_color);

  /* 内框 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_pop_up - 2, (int16_t)g_xerintosh_pop_up.y_pop_up,
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 4), pop_h, 3, g_xerintosh_draw_color);

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

#### 中文伪代码拆解

```
函数 绘制弹窗() {
    if (弹窗未运行) return

    // 第一步：到达目标位置后，记录时间戳
    if (弹窗Y == 弹窗目标Y) {
        弹窗.当前时间 = 获取Tick()
    }

    // 第二步：超时后自动收回
    if (当前时间 - 启动时间 >= 持续时间) {
        弹窗目标Y = 屏幕外上方
        if (已完全收回) {
            标记为停止运行
            return
        }
    }

    // 第三步：使用缓存高度计算布局
    弹窗高度 = 缓存高度（push时已计算）
    内容高度 = 缓存内容高度
    弹窗X = 屏幕居中
    弹窗Y = 当前Y + 弹窗高度

    // 第四步：绘制两层圆角矩形
    颜色 = 黑（背景）
    绘制圆角实心矩形(外框, 圆角半径5)

    颜色 = 白（前景）
    绘制圆角实心矩形(内框, 圆角半径3)

    // 第五步：多行文字渲染
    文字起始Y = 弹窗Y + (弹窗高 - 内容高)/2 + 字高 - 2
    for (每行文字) {
        绘制字符串(居中X+3, 文字Y, 当前行)
        文字Y += 字高 + 2
    }
}
```

**自动消失机制**：弹窗到达展开位置后记录当前时间，超过 `span` 毫秒后自动将目标位置设为屏幕外。`xerintosh_animation()` 负责平滑收回。

**多行支持**：弹窗支持自动换行（最多 `POP_UP_WRAP_LINES = 3` 行），由 `xerintosh_push_pop_up()` 在推送时计算断行点和每行宽度。

### 信息栏（Info Bar）

信息栏与弹窗结构几乎一致，区别仅在于尺寸更小（`INFO_BAR_HEIGHT = 15` vs `POP_UP_HEIGHT = 48`）和默认位置不同。

*📄 Source: [ui_draw_widgets.c](../../src/ui/ui_draw_widgets.c#L20-L72)*

---

## 与其他组件的关系

- **ui_core**：`xerintosh_ui_render_frame()` 调用 `xerintosh_draw_list()` 进行列表渲染
- **ui_item**：读取 `g_xerintosh_selector`、`g_xerintosh_camera`、`g_xerintosh_info_bar`、`g_xerintosh_pop_up` 的状态数据
- **ui_types.h**：声明 `xerintosh_is_item_visible()` 公开 API
- **hal_display**：所有 `hal_draw_*` 调用在 M5Canvas 上执行实际像素操作

---

> **See Also:** [核心引擎](core.md) | [项目系统](item.md) | [全局上下文](context.md) | [显示驱动](../hal/display.md)
