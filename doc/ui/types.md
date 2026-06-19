# 基础类型（UI Types）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [项目系统](item.md), [核心引擎](core.md)

## 概述

`ui_types.h` 是 Xerintosh UI 框架的**基础类型头文件**，集中存放所有子模块共享的枚举、回调类型、动画速度常量、布局常量和全局标志声明。

---

## 动画速度常量

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L23-L31)*

```c
#define ANIM_SPEED_LIST_ITEM    (g_anim_speed - 2)
#define ANIM_SPEED_SELECTOR     (g_anim_speed)
#define ANIM_SPEED_SELECTOR_H   (g_anim_speed + 1)
#define ANIM_SPEED_INFO_BAR     (g_anim_speed + 2)
#define ANIM_SPEED_INFO_BAR_W   (g_anim_speed + 3)
#define ANIM_SPEED_POP_UP_W     (g_anim_speed + 4)
#define ANIM_SPEED_POP_UP_Y     (g_anim_speed + 2)
#define ANIM_SPEED_CAMERA       (g_anim_speed + 4)
#define ANIM_SPEED_EXIT         (g_anim_speed + 2)
```

所有速度都基于全局 `g_anim_speed`（默认 92），通过不同偏移产生层次感的动画节奏。

### 动画内部常量

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L34-L36)*

```c
#define ANIM_SPEED_MAX          99.0f
#define ANIM_SPEED_MIN          0.0f
#define ANIM_SNAP_THRESHOLD     1.0f
```

---

## 弹簧动画参数

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L38-L44)*

```c
extern float g_spring_stiffness_selector;
extern float g_spring_damping_selector;
extern bool  g_spring_anim_mode;

#define SPRING_STIFFNESS_SELECTOR_DEFAULT  0.20f
#define SPRING_DAMPING_SELECTOR_DEFAULT    0.35f
```

Phase 2.3 引入的运行时可调弹簧参数，用于选择器 Y/W/H 动画。详见 [核心引擎](core.md) 弹簧动画章节。

---

## 回调类型

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L64-L64)*

```c
typedef void (*xerintosh_cb_t)(void *user_data);
```

所有 item 生命周期回调（init / loop / exit / destroy）统一使用该签名。

---

## 列表项类型枚举

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L71-L79)*

```c
typedef enum
{
  list_item,
  switch_item,
  slider_item,
  user_item,
  button_item,
  item_type_count,  /* 哨兵：类型数量，用于边界检查 */
} xerintosh_list_item_type_t;
```

`item_type_count` 作为哨兵，使 `ui_dispatch.c` 的 `type_in_range()` 可以用 `< item_type_count` 做边界检查，而不依赖枚举顺序。

---

## 图标类型枚举

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L84-L94)*

```c
typedef enum {
    default_icon,
    list_icon,
    switch_icon,
    plus_icon,
    user_icon,
    slider_icon,
    flag_icon,
    power_icon,
    custom_icon,    /* 自定义位图图标 */
} xerintosh_list_item_icon_t;
```

---

## 布局常量

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L96-L104)*

```c
#define MAX_LIST_CHILD_NUM 12   /* 每个父节点最多子项数 */
#define MAX_LIST_LAYER 10       /* 菜单树最大深度 */
#define LIST_ITEM_SPACING 18    /* 列表项纵向间距 */
#define LIST_ITEM_LEFT_MARGIN 4 /* 列表项左边距 */
#define LIST_ITEM_RIGHT_MARGIN 20  /* 列表项右边距 */
#define LIST_INFO_BAR_HEIGHT 3  /* 信息栏高度补偿 */
#define LIST_FONT_TOP_MARGIN 6  /* 字体顶部边距 */
```

---

## 可见性判断

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L106-L112)*

```c
extern bool xerintosh_is_item_visible(int16_t _y_item);
```

实现位于 `ui_draw_list.c`，用于跳过屏幕外项的绘制：

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L24-L27)*

```c
bool xerintosh_is_item_visible(int16_t _y_item)
{
  return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}
```

---

## 与其他组件的关系

- **ui_item_core.h**：包含 `ui_types.h` 获取类型枚举和回调类型
- **ui_dispatch.c**：使用 `item_type_count` 做边界检查
- **ui_core.c**：使用动画速度常量和弹簧参数
- **ui_draw_list.c**：使用布局常量和可见性函数

---

> **See Also:** [项目系统](item.md) | [核心引擎](core.md) | [绘制管线](drawer.md)
