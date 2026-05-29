# 核心引擎（UI Core）

> **Parent:** [知识地图](../index.md) | **Related:** [项目系统](item.md), [绘制管线](drawer.md)

## 概述

`ui_core` 是 UI 框架的**动画引擎与主循环调度器**。它负责：

1. **动画插值**：将每个元素的当前坐标平滑过渡到目标坐标
2. **相机滚动**：自动调整视口偏移，保证选择器始终在屏幕可见区域
3. **主循环**：协调列表渲染、用户页面、退场动画三者的执行顺序
4. **Widget 刷新**：驱动信息栏和弹窗的位置动画

---

## 关键概念

### 动画插值公式

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L45-L60)*

```c
void xerintosh_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (!g_anim_enabled) {
      *_pos = _pos_trg;
      return;
    }
    if (_speed >= 99.0f) _speed = 99.0f;
    if (fabs(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
    else *_pos += (_pos_trg - *_pos) / (100.0f - _speed) / 1.0f;
  }
}
```

#### 中文伪代码拆解

```
函数 动画插值(当前位置指针, 目标位置, 速度) {
    if (当前位置 == 目标位置) return

    if (abs(当前 - 目标) <= 1.0) {
        当前位置 = 目标位置     // 距离足够近，直接吸附
    } else {
        //  easing 公式：距离越大移动越快，距离越小移动越慢
        当前位置 += (目标 - 当前) / (100 - 速度)
    }
}
```

**核心思想**：这是一个**指数衰减缓动**（exponential ease-out）。每次调用只移动剩余距离的一个比例，比例由 `speed` 控制。`speed` 越接近 100，移动越快。

常见速度值：

| 元素 | 速度 | 感受 |
|------|------|------|
| 选择器移动 | 92 | 快速跟随 |
| 相机滚动 | 96 | 非常跟手 |
| 列表项展开 | 84 | 稍慢，有层次感 |
| 弹窗展开 | 94–96 | 迅速 |

### 相机（视口）滚动

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L83-L97)*

```c
void xerintosh_refresh_camera_position()
{
  /* 15 为选择器高度 */
  if (g_xerintosh_camera.selector->y_selector_trg + 15 + g_xerintosh_camera.y_camera_trg > SCREEN_HEIGHT)
    g_xerintosh_camera.y_camera_trg = SCREEN_HEIGHT - g_xerintosh_camera.selector->y_selector_trg - 15;

  if (g_xerintosh_camera.selector->y_selector_trg + g_xerintosh_camera.y_camera_trg < 0)
    g_xerintosh_camera.y_camera_trg = 0 - g_xerintosh_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  xerintosh_animation(&g_xerintosh_camera.x_camera, g_xerintosh_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  xerintosh_animation(&g_xerintosh_camera.y_camera, g_xerintosh_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}
```

#### 中文伪代码拆解

```
函数 刷新相机位置() {
    // 第一步：计算目标偏移，确保选择器在屏幕内
    选择器底部 = 选择器目标Y + 选择器高度15 + 相机目标Y
    if (选择器底部 > 屏幕高度) {
        相机目标Y = 屏幕高度 - 选择器目标Y - 15
    }

    选择器顶部 = 选择器目标Y + 相机目标Y
    if (选择器顶部 < 0) {
        相机目标Y = 0 - 选择器目标Y + 字体顶部边距
    }

    // 第二步：对当前相机位置做动画插值
    动画插值(相机当前X, 相机目标X, 速度96)
    动画插值(相机当前Y, 相机目标Y, 速度96)
}
```

**关键理解**：相机偏移量是**负数**。当列表向下滚动时，`y_camera` 变成负值，绘制时每个列表项的 `y + y_camera` 会变小，从而在屏幕上向上移动。

### 选择器位置刷新

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L139-L150)*

```c
void xerintosh_refresh_selector_position()
{
  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_selector.y_selector_trg = g_xerintosh_selector.selected_item->y_list_item_trg - oled_get_str_height() + 1;
  if (g_xerintosh_selector.selected_item->type == switch_item || g_xerintosh_selector.selected_item->type == slider_item)
    g_xerintosh_selector.w_selector_trg = SCREEN_WIDTH - 18;
  else g_xerintosh_selector.w_selector_trg = oled_get_UTF8_width(g_xerintosh_selector.selected_item->content) + 12;
  g_xerintosh_selector.h_selector_trg = oled_get_str_height() + 4;
  xerintosh_animation(&g_xerintosh_selector.y_selector, g_xerintosh_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&g_xerintosh_selector.w_selector, g_xerintosh_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&g_xerintosh_selector.h_selector, g_xerintosh_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
}
```

#### 中文伪代码拆解

```
函数 刷新选择器位置() {
    设置字体

    // 目标Y = 选中项的目标Y - 字体高度 + 1（让选择器框住文字）
    选择器目标Y = 选中项.目标Y - 字高 + 1

    // 宽度根据类型自适应
    if (选中项是开关 或 选中项是滑条) {
        选择器目标宽 = 屏幕宽 - 18    // 留右侧空间给控件
    } else {
        选择器目标宽 = 文本宽度 + 12
    }

    选择器目标高 = 15

    // 分别对Y/W/H做动画插值
    动画插值(当前Y, 目标Y, 92)
    动画插值(当前宽, 目标宽, 92)
    动画插值(当前高, 目标高, 93)
}
```

### 主循环调度

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L169-L216)*

```c
void xerintosh_ui_main_core()
{
  if (!g_in_xerintosh) return;

  /* 切换 in_user_item 的逻辑 */
  if (g_xerintosh_selector.selected_item->type == user_item
      && !xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item)
  {
    xerintosh_user_item_t *_selected_user_item = xerintosh_to_user_item(g_xerintosh_selector.selected_item);

    if (_selected_user_item->entering_user_item && g_xerintosh_exit_animation_status == 1)
    {
      if (_selected_user_item->init_function != NULL)
        _selected_user_item->init_function();
      _selected_user_item->in_user_item = 1;
    }
  }

  /* 渲染逻辑：user_item 内部由 App 自行绘制；列表模式由框架绘制 */
  if (g_xerintosh_selector.selected_item->type == user_item
      && xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item)
  {
    xerintosh_user_item_t* _selected_user_item = xerintosh_to_user_item(g_xerintosh_selector.selected_item);

    if (_selected_user_item->loop_function != NULL)
    {
      _selected_user_item->loop_function();
    }

    if (_selected_user_item->exiting_user_item && g_xerintosh_exit_animation_status == 1)
    {
        if (_selected_user_item->exit_function != NULL)
            _selected_user_item->exit_function();
        _selected_user_item->in_user_item = 0;
    }
  } else
  {
    xerintosh_refresh_camera_position();
    xerintosh_refresh_main_core_position();
    xerintosh_refresh_selector_position();
    xerintosh_draw_list();
  }

  /* 退场动画 */
  if (!g_xerintosh_exit_animation_finished)
    xerintosh_draw_exit_animation();
}
```

#### 中文伪代码拆解

```
函数 UI主循环() {
    if (不在UI模式) return

    // 第一步：处理进入用户页面的时机
    if (当前项是用户页面 且 还没进入) {
        if (正在播放进入动画 且 动画状态机到达中间态) {
            执行用户初始化回调
            标记为已进入
        }
    }

    // 第二步：渲染分支
    if (当前处于用户页面内部) {
        // 用户页面模式：执行用户loop回调
        执行用户循环回调()

        // 处理退出
        if (正在退出 且 退场动画到达中间态) {
            执行用户退出回调
            标记为已退出
        }
    } else {
        // 列表模式
        刷新相机位置()          // 保证选择器在可视区
        刷新列表项位置()        // 各列表项的Y坐标动画
        刷新选择器位置()        // 选择器框的大小和位置动画
        绘制列表()              // 调用 drawer 绘制背景和项
    }

    // 第三步：退场动画（在所有内容之上绘制遮罩）
    if (退场动画未完成) {
        绘制退场动画()
    }
}
```

### 开屏入口（已移除）

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L27-L30)*

```c
void ad_xerintosh()
{
  /* Splash screen / long-press entry removed for TFT build */
}
```

原始 OLED 框架有长按 2.5 秒进入的开屏动画。移植到 TFT 版本时已完全移除，`main.cpp` 中直接设置 `in_xerintosh = true` 进入主界面。

---

## 与其他组件的关系

- **ui_item**：读取 `xerintosh_selector`、`xerintosh_camera` 的状态，修改它们的目标坐标
- **ui_drawer**：主循环中调用 `xerintosh_draw_list()` 和 `xerintosh_draw_exit_animation()`
- **main.cpp**：`loop()` 中按顺序调用 `input_process()` → `xerintosh_ui_main_core()` → `xerintosh_ui_widget_core()` → `hal_display_flush()`

---

> **See Also:** [项目系统](item.md) | [绘制管线](drawer.md) | [输入系统](../hal/input.md)
