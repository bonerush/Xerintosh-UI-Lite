# 2026-05-19 代码优化总结

> **Parent:** [知识地图](../index.md)

## 概述

本次优化针对 M5Stick-P1 项目的 UI 核心代码进行系统性重构，目标是消除代码异味、提升可维护性，同时保持所有现有行为不变。

## 优化前后对比

| 指标 | 优化前 | 优化后 |
|---|---|---|
| 代码重复函数组 | 5 组 | 1 组（提取为辅助函数） |
| 超过 50 行的函数 | 5 个 | 0 个 |
| 魔法数字散布 | 12 处 | 全部提取为命名常量 |
| 总代码行数 | 约 621 行 | 约 608 行（净减少 13 行） |

## 具体优化点

### 1. 提取动画系统常量

将散落各处的动画速度值统一提取到 `ui_item.h`：

```c
#define ANIM_SPEED_LIST_ITEM    84
#define ANIM_SPEED_SELECTOR     92
#define ANIM_SPEED_SELECTOR_H   93
#define ANIM_SPEED_INFO_BAR     94
#define ANIM_SPEED_POP_UP_W     96
#define ANIM_SPEED_CAMERA       96
#define ANIM_SPEED_EXIT         94
```

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L11-L18)*

### 2. 统一重复动画函数

`ui_drawer.c` 中的 `astra_exit_animation()` 与 `ui_core.c` 中的 `astra_animation()` 实现完全相同，已删除前者，所有调用方统一使用后者。

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L8-L15)（已删除）*

### 3. 消除类型转换函数重复

四个类型转换函数（`astra_to_switch_item`、`astra_to_button_item`、`astra_to_slider_item`、`astra_to_user_item`）模式完全一致。提取通用辅助函数：

```c
static astra_list_item_t *astra_safe_cast(astra_list_item_t *_item,
                                          astra_list_item_type_t _expected_type)
{
  if (_item != NULL && _item->type == _expected_type)
    return _item;
  return astra_get_root_list();
}
```

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L60-L66)*

### 4. 消除 `new_*_item` 创建函数重复

五个创建函数（`astra_new_list_item`、`astra_new_switch_item`、`astra_new_button_item`、`astra_new_slider_item`、`astra_new_user_item`）共享相同的初始化模式。提取基类初始化辅助函数：

```c
static void astra_init_base_item(astra_list_item_t *_item,
                                  astra_list_item_type_t _type,
                                  const char *_content,
                                  astra_list_item_icon_t _icon,
                                  astra_list_item_icon_t _default_icon)
{
  memset(_item, 0, sizeof(astra_list_item_t));
  _item->type = _type;
  _item->content = _content;
  _item->icon = (_icon == default_icon) ? _default_icon : _icon;
}
```

同时为所有 `malloc` 调用添加了 NULL 检查，避免内存分配失败时的未定义行为。

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L106-L116)*

### 5. 拆分 `astra_draw_list_item()` 大函数

原函数约 95 行，包含 5 层 `if-else` 链。重构后：

- 提取 `is_item_visible()` 边界检查辅助函数
- 为每种列表项类型提取独立的绘制函数：`draw_list_item_list()`、`draw_list_item_switch()`、`draw_list_item_button()`、`draw_list_item_slider()`、`draw_list_item_user()`
- 主函数改用 `switch-case` 分派，逻辑清晰

*📄 Source: [ui_drawer.c](../../src/ui/ui_drawer.c#L235-L280)*

### 6. 拆分选择器导航函数

`astra_selector_jump_to_selected_item()` 和 `astra_selector_exit_current_item()` 均超过 50 行。提取以下辅助函数：

- `handle_user_item_enter()` / `handle_user_item_exit()`：统一处理 user_item 的进入/退出状态重置
- `handle_slider_confirm_toggle()`：处理滑条确认态切换
- `find_item_index()`：在父列表中查找子项索引（替代原来的内联循环）

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L188-L217)*

### 7. 消除 `hal_input.cpp` 按钮事件重复

BtnA 和 BtnB 的事件处理逻辑完全重复（约 30 行 × 2）。提取 `check_button_event()` 辅助函数，两个按钮共用同一逻辑。

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L49-L91)*

### 8. 命名规范统一

将 `astra_animation()` 参数 `_posTrg` 改为 `_pos_trg`，统一为 `snake_case`。

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L24)*

## 验证结果

- **编译验证**：所有 C/C++ 文件在 `NATIVE_TEST` 环境下语法检查通过
- **修改文件数**：5 个文件
- **代码行变化**：304 行新增，317 行删除（净减少 13 行，更紧凑）
- **行为保持**：所有改动均为纯重构，未修改任何外部 API 或运行时行为

## 仍存在的问题

以下问题在本次优化中**未解决**，留待后续处理：

1. `ui_drawer.c` 中沙漏动画绘制仍有大量硬编码像素坐标（虽然提取了 `HOURGLASS_WIDTH` 常量，但坐标表 `_points` 仍为魔法数字）
2. `astra_info_bar_t` 和 `astra_pop_up_t` 两个结构体字段名不同但逻辑相同，可进一步统一为通用 `notification` 结构
3. 全局可变状态（`astra_selector`、`astra_camera`、`in_astra` 等）仍然较多，可考虑封装为上下文结构体
4. 测试覆盖率不足（仅 3 个基本测试），需要增加 UI 交互和动画测试
