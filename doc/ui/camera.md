# 相机/视口（UI Camera）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [选择器](selector.md), [核心引擎](core.md), [项目系统](item.md)

## 概述

`ui_camera` 不是硬件相机，而是**视口偏移量**。它负责在列表滚动时调整全局 Y 偏移，使当前选中的选择器始终处于屏幕可视区域内。

---

## 数据结构

*📄 Source: [ui_camera.h](../../src/ui/ui_camera.h#L24-L28)*

```c
typedef struct xerintosh_camera_t
{
  float x_camera, x_camera_trg, y_camera, y_camera_trg;  /* 当前与目标偏移 */
  xerintosh_selector_t *selector;  /* 绑定的选择器 */
} xerintosh_camera_t;
```

| 字段 | 说明 |
|---|---|
| `x_camera` / `y_camera` | 当前视口偏移 |
| `x_camera_trg` / `y_camera_trg` | 目标视口偏移 |
| `selector` | 绑定的选择器指针，相机根据选择器位置计算偏移 |

---

## 绑定

*📄 Source: [ui_item_camera.c](../../src/ui/ui_item_camera.c#L19-L24)*

选择器在首次挂载到根节点时，会同步绑定到相机：

```c
xerintosh_bind_selector_to_camera(&g_xerintosh_selector);
```

绑定后，`xerintosh_refresh_camera_position()` 即可根据选择器位置自动调整视口。

---

## 视口滚动算法

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L178-L192)*

```c
void xerintosh_refresh_camera_position()
{
  if (g_xerintosh_camera.selector == NULL) return;
  if (g_xerintosh_camera.selector->selected_item == NULL) return;

  /* 15 为选择器高度 */
  if (g_xerintosh_camera.selector->y_selector_trg + 15 + g_xerintosh_camera.y_camera_trg > SCREEN_HEIGHT)
    g_xerintosh_camera.y_camera_trg = SCREEN_HEIGHT - g_xerintosh_camera.selector->y_selector_trg - 15;

  if (g_xerintosh_camera.selector->y_selector_trg + g_xerintosh_camera.y_camera_trg < 0)
    g_xerintosh_camera.y_camera_trg = 0 - g_xerintosh_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  xerintosh_animation(&g_xerintosh_camera.x_camera, g_xerintosh_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  xerintosh_animation(&g_xerintosh_camera.y_camera, g_xerintosh_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}
```

### 中文伪代码拆解

```
函数 刷新相机位置() {
    if (选择器未绑定) return
    if (未选中任何项) return

    // 向下越界：选择器底部超出屏幕
    if (选择器目标Y + 15 + 相机目标Y > 屏幕高度) {
        相机目标Y = 屏幕高度 - 选择器目标Y - 15
    }

    // 向上越界：选择器顶部超出屏幕
    if (选择器目标Y + 相机目标Y < 0) {
        相机目标Y = -选择器目标Y + 字体顶部边距
    }

    // 动画插值到目标偏移
    动画插值(相机当前X, 相机目标X, 速度96)
    动画插值(相机当前Y, 相机目标Y, 速度96)
}
```

**关键理解**：相机偏移量通常为负数。当列表向下滚动时，`y_camera` 变得更负，绘制时每个列表项的 `y + y_camera` 变小，从而在屏幕上向上移动。

---

## 与选择器的关系

- 选择器决定"当前看哪里"
- 相机决定"屏幕显示哪个区域"
- 两者配合确保选择器不会跑到屏幕外

---

> **See Also:** [选择器](selector.md) | [核心引擎](core.md) | [项目系统](item.md) | [绘制管线](drawer.md)
