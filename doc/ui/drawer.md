# 绘制管线（UI Drawer）

> **Parent:** [知识地图](../index.md) | **Related:** [核心引擎](core.md), [项目系统](item.md), [绘制驱动适配](draw-driver.md)

## 概述

`ui_drawer` 是 UI 框架的**渲染层**，负责把数据模型（`ui_item` 中的列表项、选择器、弹窗等）转换为实际的像素绘制指令。所有绘制通过 `oled_*` 宏（实际映射到 `hal_*`）输出到 TFT 后台缓冲区。

---

## 关键概念

### 列表外观（滚动条 + 装饰线）

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L191-L242)*

```c
void astra_draw_list_appearance()
{
  oled_set_draw_color(1);
  oled_draw_H_line(0, 1, 66);
  oled_draw_H_line(0, 0, 67);

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
      oled_draw_pixel(i, draw_cfg[j]._y);

  // 右侧滚动条背景
  oled_draw_V_line(SCREEN_WIDTH - 5, 0, SCREEN_HEIGHT);
  oled_draw_V_line(SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT);

  // 滚动条滑块
  static float _length_each_part = 0;
  _length_each_part = ceilf((SCREEN_HEIGHT - 10.0f) / (float) astra_selector.selected_item->parent->child_num);
  oled_draw_box(SCREEN_WIDTH - 4, 5 + astra_selector.selected_index * _length_each_part, 3, _length_each_part);
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

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L244-L340)*

```c
void astra_draw_list_item()
{
  for (unsigned char i = 0; i < astra_selector.selected_item->parent->child_num; i++)
  {
    int16_t _x_list_item = astra_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
    int16_t _y_list_item = astra_selector.selected_item->parent->child_list_item[i]->y_list_item
                           + astra_camera.y_camera - oled_get_str_height()/2;

    oled_set_draw_color(1);

    if (astra_selector.selected_item->parent->child_list_item[i]->type == list_item) {
      if (_y_list_item + 2 > LIST_INFO_BAR_HEIGHT && _y_list_item - 2 < SCREEN_HEIGHT) {
        astra_draw_list_icon(...);
      }
    }
    else if (...) {
      // switch_item: 绘制外框 + 滑块
      // slider_item: 绘制数值（编辑时闪烁背景）
      // button_item: 仅绘制图标
    }

    astra_set_font(NULL);
    if (_y_list_item + oled_get_str_height() / 2 > LIST_INFO_BAR_HEIGHT &&
        _y_list_item + oled_get_str_height() / 2 < SCREEN_HEIGHT) {
      oled_draw_UTF8(10 + _x_list_item, _y_list_item + oled_get_str_height() / 2,
                   astra_selector.selected_item->parent->child_list_item[i]->content);
    }
  }
}
```

#### 中文伪代码拆解

```
函数 绘制列表项() {
    for (遍历当前菜单的所有子项) {
        项 = 子项数组[i]

        // 计算屏幕坐标 = 列表坐标 + 相机偏移
        屏幕X = 相机X + 左边距4
        屏幕Y = 项.当前Y + 相机Y - 字体高度/2

        // 裁剪：只绘制在可视区内的项
        if (屏幕Y 在有效范围内) {
            switch (项的类型) {
                case 普通列表项:
                    绘制图标(项的图标, 屏幕X, 屏幕Y)
                    break

                case 开关项:
                    绘制图标(...)
                    绘制外框(屏幕右侧, 屏幕Y, 宽11, 高7)
                    if (值为真) {
                        绘制滑块在右侧
                    } else {
                        绘制滑块在左侧
                    }
                    break

                case 滑条项:
                    绘制图标(...)
                    格式化数值为字符串
                    if (处于编辑状态) {
                        // 闪烁效果：每1秒切换背景可见性
                        if (背景可见) 绘制圆角背景
                        用反色绘制数值字符串
                    } else {
                        正常绘制数值字符串
                    }
                    break
            }
        }

        // 绘制文本标签（统一在图标右侧）
        if (文本中心在可视区) {
            绘制UTF8(屏幕X+10, 屏幕Y+字高/2, 项的文本)
        }
    }
}
```

**裁剪逻辑**：`SCREEN_HEIGHT = 160`，`LIST_INFO_BAR_HEIGHT = 3`。所有项只在大于 3px 且小于 160px 的 Y 范围内绘制，避免越界和无效绘制。

### 选择器高亮（XOR 反色）

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L384-L404)*

```c
void astra_draw_selector()
{
  int16_t _x_selector = astra_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
  int16_t _y_selector = astra_selector.y_selector + astra_camera.y_camera;

  hal_draw_xor_rect(_x_selector, _y_selector, astra_selector.w_selector, astra_selector.h_selector);

  oled_set_draw_color(1);
  for (int16_t i = astra_selector.w_selector + _x_selector;
       i <= astra_selector.w_selector + _x_selector + 7; i += 2) {
    for (int16_t j = _y_selector;
         j <= _y_selector + astra_selector.h_selector - 1; j++) {
      if (j % 2 == 0)
        oled_draw_pixel(i + 1, j);
      if (j % 2 == 1)
        oled_draw_pixel(i, j);
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

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L151-L189)*

```c
void astra_draw_pop_up()
{
  if (!astra_pop_up.is_running) return;

  if (astra_pop_up.y_pop_up == astra_pop_up.y_pop_up_trg)
    astra_pop_up.time = get_ticks();

  if (astra_pop_up.time - astra_pop_up.time_start >= astra_pop_up.span) {
    astra_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
    if (astra_pop_up.y_pop_up == astra_pop_up.y_pop_up_trg)
      astra_pop_up.is_running = false;
  }

  // 绘制三层嵌套圆角矩形，营造"浮雕"边框效果
  oled_set_draw_color(1);
  oled_draw_R_box(_x_pop_up + 1, (int16_t)astra_pop_up.y_pop_up + 3, ...);   // 外层阴影

  oled_set_draw_color(0);
  oled_draw_R_box((int16_t)(SCREEN_WIDTH/2 - (astra_pop_up.w_pop_up + 4)/2 - 2), ...); // 中层底色

  oled_set_draw_color(1);
  oled_draw_R_box(_x_pop_up - 2, (int16_t)astra_pop_up.y_pop_up, ...);       // 内层主体

  oled_draw_UTF8(_x_pop_up + 3,
                 (int16_t)(astra_pop_up.y_pop_up + oled_get_str_height() + 1),
                 astra_pop_up.content);
}
```

#### 中文伪代码拆解

```
函数 绘制弹窗() {
    if (弹窗未运行) return

    // 第一步：到达目标位置后，开始计时
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

    // 第三步：三层嵌套绘制，营造立体边框
    颜色 = 白
    绘制圆角实心矩形(偏移+1, y+3, ...)     // 最外层：白色阴影

    颜色 = 黑
    绘制圆角实心矩形(居中, y-2, ...)       // 中层：黑色边框

    颜色 = 白
    绘制圆角实心矩形(偏移-2, y, ...)       // 内层：白色主体

    // 第四步：绘制文字
    绘制UTF8(居中X+3, 弹窗Y+字高+1, 弹窗内容)
}
```

**自动消失机制**：弹窗到达展开位置后记录当前时间，超过 `span` 毫秒后自动将目标位置设为屏幕外。`astra_animation()` 负责平滑收回。

### 信息栏（Info Bar）

信息栏与弹窗结构几乎一致，区别仅在于尺寸更小（`INFO_BAR_HEIGHT = 15` vs `POP_UP_HEIGHT = 20`）和默认位置不同。

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L110-L149)*

---

## 与其他组件的关系

- **ui_core**：主循环中按顺序调用 `astra_draw_list()` → `astra_draw_widget()` → `astra_draw_exit_animation()`
- **ui_item**：读取 `astra_selector`、`astra_camera`、`astra_info_bar`、`astra_pop_up` 的状态数据
- **hal_display**：所有 `oled_*` 宏最终落入 `hal_draw_*`，在 M5Canvas 上执行实际像素操作

---

> **See Also:** [核心引擎](core.md) | [项目系统](item.md) | [显示驱动](../hal/display.md)
