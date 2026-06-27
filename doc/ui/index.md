# UI 核心框架

> **Parent:** [doc/index.md](../index.md)

## 概述

UI 核心框架负责 Xerintosh 的菜单树管理、输入分派、动画与渲染。它以 C 结构体模拟面向对象，所有可见项统一继承自 `xerintosh_list_item_t` 基类，并通过 `ui_dispatch.c` 按类型分派事件。

## 核心模块

| 模块 | 源文件 | 职责 |
|---|---|---|
| 类型系统 | [ui_types.h](../../src/ui/ui_types.h) | 枚举、常量、回调类型定义 |
| 项基类 | [ui_item_core.h](../../src/ui/ui_item_core.h) | `xerintosh_list_item_t` 基类与核心 API |
| 聚合头 | [ui_item.h](../../src/ui/ui_item.h) | 向后兼容的聚合头，包含所有 UI 子模块 |
| 上下文 | [ui_context.h](../../src/ui/ui_context.h)、[ui_context.c](../../src/ui/ui_context.c) | 全局 UI 上下文（选择器、相机、信息栏、弹窗） |
| 输入分派 | [ui_dispatch.c](../../src/ui/ui_dispatch.c) | 按项类型分派确认/上下/退出事件 |
| 选择器 | [ui_item_selector.c](../../src/ui/ui_item_selector.c)、[ui_selector.h](../../src/ui/ui_selector.h) | 当前选中项、动画、导航 |
| 列表绘制 | [ui_draw_list.c](../../src/ui/ui_draw_list.c) | 菜单列表、滚动条、文字滚动 |
| 退场动画 | [ui_draw_anim.c](../../src/ui/ui_draw_anim.c) | 层级退出时的沙漏/扫描线动画 |
| 弹窗 | [ui_item_popup.c](../../src/ui/ui_item_popup.c) | 确认/提示弹窗绘制 |
| 脏区域 | [ui_dirty.h](../../src/ui/ui_dirty.h)、[ui_dirty.c](../../src/ui/ui_dirty.c) | 脏区域追踪（当前仍全屏重绘） |

## 关键概念

### 列表项类型

`xerintosh_list_item_type_t` 定义所有支持的项类型：

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L108-L115)*

```c
typedef enum {
  list_item,
  switch_item,
  button_item,
  slider_item,
  user_item,
  item_type_count,  /* 哨兵：类型数量，用于边界检查 */
} xerintosh_list_item_type_t;
```

### 基类结构

所有项的第一个字段必须是 `xerintosh_list_item_t` 基类，以实现 C 风格多态：

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L35-L57)*

```c
typedef struct xerintosh_list_item_t
{
  xerintosh_list_item_type_t type;
  xerintosh_list_item_icon_t icon;
  const char *content;
  uint8_t layer;
  float y_list_item, y_list_item_trg;
  uint8_t child_num;
  struct xerintosh_list_item_t *child_list_item[MAX_LIST_CHILD_NUM];
  struct xerintosh_list_item_t *parent;
  void *user_data;
  /* ... */
} xerintosh_list_item_t;
```

### 派生类型

- **switch_item**：绑定 `bool*`，`xerintosh_new_switch_item()` 创建
- **slider_item**：绑定 `int16_t*`，支持最小/最大/步进
- **button_item**：按下时触发一次性回调
- **user_item**：全屏 App 入口，提供 `init/loop/exit` 生命周期

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L66-L124)*

## 常用 API

- `xerintosh_get_root_list()`：获取根菜单（单例）
- `xerintosh_new_list_item()` / `xerintosh_new_switch_item()` / `xerintosh_new_slider_item()` / `xerintosh_new_button_item()` / `xerintosh_new_user_item()`：创建各类项
- `xerintosh_push_item_to_list()`：将子项挂载到父项
- `xerintosh_clear_children_of_list()`：清空并释放父项下所有子项

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L127-L253)*

## 中文伪代码拆解（菜单挂载）

```
函数 创建并挂载菜单项(父项, 创建函数, 名称) {
    子项 = 创建函数(名称, 图标, 回调, ...)
    if (子项 == NULL) {
        记录错误日志("创建 %s 失败", 名称)
        return false
    }
    if (!xerintosh_push_item_to_list(父项, 子项)) {
        记录错误日志("挂载 %s 失败", 名称)
        return false
    }
    return true
}
```

**核心思想**：每个菜单项都是树节点，通过 `child_list_item[]` 和 `parent` 指针形成树，渲染与导航均在这棵树上进行。

## 安全约束

- 传给 `switch_item` / `slider_item` 的 `value` 指针必须指向全局或 static 变量，严禁局部变量。
- `button_item` / `user_item` 的回调中禁止直接调用 `xerintosh_push_pop_up()` 等显示层函数，应设标志位由主循环统一处理。
- `user_item` 的 `init()` / `exit()` 中必须调用 `hal_input_reset_events()` 清除残留按键。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L77-L225)*

---

> **See Also:** [App 层文档](../app/index.md) | [API 模板教程](../tutorials/api-templates.md)
