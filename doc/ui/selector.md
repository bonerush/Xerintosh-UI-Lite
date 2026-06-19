# 选择器（UI Selector）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [项目系统](item.md), [核心引擎](core.md), [相机](camera.md)

## 概述

选择器是 Xerintosh 菜单框架中的**高亮框**，标识当前选中的列表项。它不仅有当前位置/尺寸（`y/w/h`），还有目标位置/尺寸（`y_trg/w_trg/h_trg`），每帧通过动画插值平滑移动。

---

## 数据结构

*📄 Source: [ui_selector.h](../../src/ui/ui_selector.h#L24-L30)*

```c
typedef struct xerintosh_selector_t
{
  float y_selector, y_selector_trg, w_selector, w_selector_trg, h_selector, h_selector_trg;
  float v_y_selector, v_w_selector, v_h_selector;  /* 弹簧动画速度状态 */
  uint8_t selected_index;        /* 当前选中索引 */
  xerintosh_list_item_t *selected_item;  /* 当前选中项指针 */
} xerintosh_selector_t;
```

| 字段 | 说明 |
|---|---|
| `y/w/h_selector` | 选择器当前位置与尺寸 |
| `*_trg` | 目标位置与尺寸，由 `xerintosh_refresh_selector_position()` 根据当前选中项计算 |
| `v_*_selector` | 弹簧动画速度状态（仅弹簧模式使用） |
| `selected_index` | 在当前父项子项数组中的索引 |
| `selected_item` | 指向当前选中列表项的指针 |

---

## 绑定

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L41-L63)*

```c
bool xerintosh_bind_item_to_selector(xerintosh_list_item_t *_item)
{
  if (_item == NULL) return false;
  if (_item->parent == NULL) return false;

  if (g_xerintosh_selector.selected_item == NULL)
  {
    g_xerintosh_selector.y_selector = 2 * SCREEN_HEIGHT;
    g_xerintosh_selector.h_selector = SCREEN_HEIGHT;
  }
  g_xerintosh_selector.selected_index = find_item_index(_item->parent, _item);
  g_xerintosh_selector.selected_item = _item;

  /* 弹簧动画：切换目标时清零速度 */
  g_xerintosh_selector.v_y_selector = 0.0f;
  g_xerintosh_selector.v_w_selector = 0.0f;
  g_xerintosh_selector.v_h_selector = 0.0f;

  xerintosh_invalidate();
  return true;
}
```

绑定操作在根节点第一个子项被 `xerintosh_push_item_to_list()` 挂载时自动触发，也可在测试中手动调用。

---

## 导航

### 下一项 / 上一项

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L72-L100) / [ui_item_selector.c](../../src/ui/ui_item_selector.c#L107-L135)*

```c
void xerintosh_selector_go_next_item();
void xerintosh_selector_go_prev_item();
```

两者都遵循相同模式：
1. 先通过 `xerintosh_dispatch_input_next/prev()` 处理类型特定输入（如 slider 编辑模式增减值）
2. 若未被消费，则执行默认循环导航
3. 到达边界时回绕到另一端
4. 切换目标时清零弹簧速度，防止旧速度影响新目标

### 确认 / 返回

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L141-L186)*

```c
void xerintosh_selector_jump_to_selected_item(void);  /* 确认/进入 */
void xerintosh_selector_exit_current_item(void);       /* 返回/退出 */
```

确认操作委托给 `xerintosh_dispatch_enter()`，返回操作委托给 `xerintosh_dispatch_input_exit()`。两者都加入了 NULL 检查：根节点无父项、子项清空后 `count == 0`、返回时祖父节点为 NULL 等场景都有防御性处理。

---

## 安全辅助函数

### user_item 通用退出检测

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L190-L200)*

```c
bool ui_user_item_try_exit(hal_event_t event_b)
{
    if (event_b != HAL_EVENT_LONG_PRESS) return false;

    xerintosh_user_item_t *current =
        xerintosh_to_user_item(g_xerintosh_selector.selected_item);
    if (current != NULL && !current->exiting_user_item) {
        xerintosh_selector_exit_current_item();
    }
    return true;
}
```

在 `user_item` 的 `loop()` 中调用，长按 B 时自动触发退出。返回 `true` 表示事件已被消费。

### 安全移出

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L231-L279)*

```c
void ui_selector_safety_move_out(xerintosh_list_item_t *subtree_root,
                                 xerintosh_list_item_t *fallback_parent);
```

在移除或清空子树前调用。如果当前选择器位于即将被移除的子树内，会将其移动到 `fallback_parent` 的安全子项上；若所有子项都在待移除子树内，则回退到 `fallback_parent` 本身。避免销毁子树后选择器成为悬垂指针。

### 重建锚定

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L202-L229)*

```c
void ui_selector_rebuild_anchor(xerintosh_list_item_t *subtree_root,
                                xerintosh_list_item_t *parent);
```

在动态重建子菜单前调用。若选择器位于即将重建的子树内，将其提升到 `subtree_root`，并计算在 `parent` 子项数组中的索引。这样可以避免后续重新挂载子项时索引和指针混乱。

---

## 绘制

选择器高亮框由 `xerintosh_draw_selector()` 在 `ui_draw_list.c` 中绘制：

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L207-L229)*

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

---

## 与其他组件的关系

- **ui_core.c**：`xerintosh_refresh_selector_position()` 每帧更新选择器目标位置与尺寸
- **ui_camera**：相机根据选择器目标坐标调整视口偏移
- **ui_draw_list.c**：`xerintosh_draw_selector()` 绘制高亮框
- **ui_dispatch.c**：确认/导航输入按 item 类型派发

---

> **See Also:** [项目系统](item.md) | [核心引擎](core.md) | [相机](camera.md) | [绘制管线](drawer.md)
