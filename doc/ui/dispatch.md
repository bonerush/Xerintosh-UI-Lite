# 类型派发表（UI Dispatch）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [项目系统](item.md), [核心引擎](core.md)

## 概述

`ui_dispatch.c` 是 UI 框架的**类型派发层**，使用**函数指针 vtable** 集中管理所有 item 类型相关的生命周期行为。

在重构前，类型判断以 `if (type == user_item) ... else if (type == switch_item) ...` 的内联链形式散落在 `xerintosh_selector_jump_to_selected_item()`、`xerintosh_selector_go_next_item()`、`xerintosh_draw_list_item()`、`xerintosh_destroy_item_tree()` 等核心函数中。每次新增 Item 类型都需要修改多个文件，违反了开闭原则。

重构后的派发表将**"根据类型选择行为"**的过程从分散的条件分支变为 O(1) 数组索引，所有类型特定逻辑集中到 `ui_dispatch.c`，代码更清晰、可扩展性更强。

---

## 关键概念

### 生命周期 vtable

```
调用方
  │
  ├─ xerintosh_dispatch_enter(item)      → 确认/进入
  ├─ xerintosh_dispatch_input_next(item) → 下一项输入
  ├─ xerintosh_dispatch_input_prev(item) → 上一项输入
  ├─ xerintosh_dispatch_input_exit(item) → 返回/退出输入
  ├─ xerintosh_dispatch_measure(item)    → 选择器宽度
  ├─ xerintosh_dispatch_draw(item,x,y)   → 列表项绘制
  ├─ xerintosh_dispatch_draw_overlay(item) → 覆盖层绘制
  ├─ xerintosh_dispatch_destroy(item)    → 类型特定销毁
  └─ xerintosh_dispatch_has_right_control(item) → 是否有右侧控件
```

每个公开函数都会执行 NULL 检查与类型范围检查，再索引到 `s_dispatch[item->type]` 对应的处理函数。

### 函数指针类型

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c)*

```c
typedef struct {
    void (*enter)(xerintosh_list_item_t *);
    bool (*input_next)(xerintosh_list_item_t *);
    bool (*input_prev)(xerintosh_list_item_t *);
    bool (*input_exit)(xerintosh_list_item_t *);
    int16_t (*measure)(xerintosh_list_item_t *);
    void (*draw)(xerintosh_list_item_t *, int16_t, int16_t);
    void (*draw_overlay)(xerintosh_list_item_t *);
    void (*destroy)(xerintosh_list_item_t *);
    bool (*has_right_control)(xerintosh_list_item_t *);
} xerintosh_dispatch_vtable_t;
```

输入类函数（`input_next` / `input_prev` / `input_exit`）返回 `bool`：
- `true` 表示输入已被类型特定逻辑消费（如 slider 编辑模式增减值、user_item 运行态忽略导航）。
- `false` 表示未消费，调用方应执行默认导航。

### 派发表定义

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c)*

```c
static const xerintosh_dispatch_vtable_t s_dispatch[] = {
    [list_item]   = { .enter = dispatch_enter_list,   .input_next = ..., ... },
    [switch_item] = { .enter = dispatch_enter_switch, .input_next = ..., ... },
    [slider_item] = { .enter = dispatch_enter_slider, .input_next = ..., ... },
    [user_item]   = { .enter = dispatch_enter_user,   .input_next = ..., ... },
    [button_item] = { .enter = dispatch_enter_button, .input_next = ..., ... },
};
```

使用 C99 **指定初始化器**（designated initializer）语法 `[list_item] = { ... }`，按枚举值索引到对应的处理函数组。未指定的函数指针自动置零，由调用方在运行时检查防护。

### 公开派发函数

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c)*

```c
void xerintosh_dispatch_enter(xerintosh_list_item_t *item);
bool xerintosh_dispatch_input_next(xerintosh_list_item_t *item);
bool xerintosh_dispatch_input_prev(xerintosh_list_item_t *item);
bool xerintosh_dispatch_input_exit(xerintosh_list_item_t *item);
int16_t xerintosh_dispatch_measure(xerintosh_list_item_t *item);
void xerintosh_dispatch_draw(xerintosh_list_item_t *item, int16_t x, int16_t y);
void xerintosh_dispatch_draw_overlay(xerintosh_list_item_t *item);
void xerintosh_dispatch_destroy(xerintosh_list_item_t *item);
bool xerintosh_dispatch_has_right_control(xerintosh_list_item_t *item);
```

两层防护：
1. **NULL 检查**：防止空指针解引用
2. **类型范围检查**：`item->type > button_item` 防护越界数组访问（如果 `type` 枚举值被意外修改或内存损坏）

---

## 各类型处理函数详解

### dispatch_enter_list — 进入子菜单

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c-L64)*

```c
static void dispatch_enter_list(xerintosh_list_item_t *item)
{
    if (item->child_num == 0) return;
    g_xerintosh_refresh_list_value = true;
    for (uint8_t i = 0; i < item->child_num; i++)
        item->child_list_item[i]->y_list_item = 0;
    g_xerintosh_selector.selected_index = 0;
    g_xerintosh_selector.selected_item = item->child_list_item[0];
    if (item->init_function) {
        item->init_function(item->user_data);
    }
}
```

#### 中文伪代码拆解

```
函数 进入子菜单(列表项) {
    if (没有子项) return   // 空菜单，不操作

    标记需要刷新列表值 = true

    // 重置所有子项的 Y 坐标（从 0 开始做入场动画）
    for (每个子项) {
        子项.当前Y坐标 = 0
    }

    // 选择器移动到第一个子项
    选择器.选中索引 = 0
    选择器.选中项 = 第一个子项
}
```

**核心思想**：将子项的 `y_list_item`（当前坐标）重置为 0，配合 `y_list_item_trg`（已在 `xerintosh_push_item_to_list` 中设置的目标坐标），触发入场滑入动画。

### dispatch_enter_user — 进入全屏 App

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c-L25)*

```c
static void dispatch_enter_user(xerintosh_list_item_t *item)
{
    xerintosh_user_item_t *user = xerintosh_to_user_item(item);
    g_xerintosh_exit_animation_finished = false;
    g_xerintosh_exit_animation_status = 0;
    user->entering_user_item = true;
    user->exiting_user_item = false;
    if (user->kernel_pid == KERN_PID_INVALID)
        user->kernel_pid = kern_task_register_virtual(user->base_item.content);
}
```

#### 中文伪代码拆解

```
函数 进入用户App(列表项) {
    用户项 = 安全转换为用户项(列表项)

    // 初始化退场动画状态机
    退场动画完成标志 = false
    退场动画阶段 = 0        // 从阶段0开始（遮罩向下展开）

    // 设置用户项生命周期标志
    用户项.正在进入 = true   // 下一帧由 ui_core 触发 init_function
    用户项.正在退出 = false  // 确保退出标志不残留

    // 注册内核虚任务（使 App 在 /proc/tasks 可见，可通过 kill 终止）
    if (尚未注册内核PID) {
        内核PID = 注册虚任务(用户项.显示文本)
    }
}
```

**关键时序**：这里只设置标志位，实际 `init_function` 的调用由 `xerintosh_ui_update_lifecycle()` 在退场动画到达阶段 1（遮罩到达底部）时触发，实现"退场动画播放完毕 → App 初始化"的无缝衔接。

### dispatch_enter_switch — 切换开关

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c-L32)*

```c
static void dispatch_enter_switch(xerintosh_list_item_t *item)
{
    xerintosh_switch_item_t *sw = xerintosh_to_switch_item(item);
    *sw->value = !*sw->value;
    if (sw->exit_function) sw->exit_function(item->user_data);
}
```

翻转绑定的布尔值指针，然后触发副作用回调。`exit_function` 的命名可能引起误解——它在确认操作时触发，不仅仅是"退出"时才调用。它用于保存设置到 NVS、更新 UI 等副作用。

### dispatch_enter_slider — 滑条确认/退出编辑

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c-L51)*

```c
static void dispatch_enter_slider(xerintosh_list_item_t *item)
{
    xerintosh_slider_item_t *sl = xerintosh_to_slider_item(item);
    if (!sl->is_confirmed) {
        sl->is_confirmed = true;
        sl->value_backup = *sl->value;
        return;
    }
    if (sl->exit_function) sl->exit_function(item->user_data);
    sl->is_confirmed = false;
}
```

滑条使用**两阶段确认**机制：

| 确认次数 | 状态 | 行为 |
|----------|------|------|
| 第一次 | 进入编辑模式 | 备份当前值，设置 `is_confirmed = true` |
| 第二次 | 退出编辑模式 | 触发 `exit_function` 持久化，清除 `is_confirmed` |

`value_backup` 用于长按取消时恢复原值（由 `xerintosh_dispatch_input_exit()` 处理）。

### dispatch_enter_button — 触发按钮回调

*📄 Source: [ui_dispatch.c](../../src/ui/ui_dispatch.c-L39)*

```c
static void dispatch_enter_button(xerintosh_list_item_t *item)
{
    xerintosh_button_item_t *btn = xerintosh_to_button_item(item);
    if (btn->exit_function) btn->exit_function(item->user_data);
}
```

最简单的处理函数：直接触发回调。按钮是一次性操作，不维护任何状态。

---

## 与重构前的对比

### 重构前：内联 switch 链

*📄 原始代码结构（已移除）*

```c
void xerintosh_selector_jump_to_selected_item()
{
    if (!g_in_xerintosh) return;

    if (g_xerintosh_selector.selected_item->type == user_item) {
        handle_user_item_enter(...);  // ~10 行
        return;
    }
    if (g_xerintosh_selector.selected_item->type == switch_item) {
        // ~6 行内联
        return;
    }
    if (g_xerintosh_selector.selected_item->type == button_item) {
        // ~4 行内联
        return;
    }
    if (g_xerintosh_selector.selected_item->type == slider_item) {
        handle_slider_confirm_toggle(...);  // ~8 行
        return;
    }
    // list_item 默认路径：~12 行内联
}
```

### 重构后：按行为统一派发

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c)*

```c
void xerintosh_selector_jump_to_selected_item()
{
    if (!g_in_xerintosh) return;
    if (g_xerintosh_selector.selected_item == NULL) return;
    xerintosh_dispatch_enter(g_xerintosh_selector.selected_item);
}

void xerintosh_selector_go_next_item()
{
    if (g_xerintosh_selector.selected_item == NULL) return;
    if (xerintosh_dispatch_input_next(g_xerintosh_selector.selected_item)) return;
    // 默认循环导航 ...
}

void xerintosh_selector_exit_current_item()
{
    if (g_xerintosh_selector.selected_item == NULL) return;
    if (xerintosh_dispatch_input_exit(g_xerintosh_selector.selected_item)) return;
    // 默认返回导航 ...
}
```

### 收益分析

| 维度 | 重构前 | 重构后 |
|------|--------|--------|
| 类型判断分布 | 散落在 selector / draw / destroy / core 等多个文件 | 全部集中在 `ui_dispatch.c` |
| 新增 Item 类型时的修改点 | 修改多个核心函数 + 可能新增辅助函数 | 新增对应 `dispatch_*` 函数 + 在 vtable 中加一行 |
| 类型-行为映射 | O(n) 线性查找（最坏遍历全部 if） | O(1) 数组索引 |
| 每个类型的逻辑可见性 | 混在一起，需滚动阅读 | 独立函数，一目了然 |
| 单元测试难度 | 需 mock 全局状态才能测试单类型 | 可直接测试公开派发函数 |

---

## 扩展指南：如何新增 Item 类型

假设要新增一个 `color_item`（颜色选择器）：

1. 在 `ui_types.h` 的 `xerintosh_list_item_type_t` 枚举中添加 `color_item`
2. 在 `ui_item_core.h` 中定义 `xerintosh_color_item_t` 结构体
3. 在 `ui_dispatch.c` 中添加所需生命周期的处理函数：
   ```c
   static void dispatch_enter_color(xerintosh_list_item_t *item)
   {
       xerintosh_color_item_t *clr = xerintosh_to_color_item(item);
       // 实现颜色选择进入逻辑
   }

   static bool dispatch_input_next_color(xerintosh_list_item_t *item)
   {
       // 颜色选择器下一个颜色
       return true; // 消费输入
   }

   static int16_t dispatch_measure_color(xerintosh_list_item_t *item)
   {
       (void)item;
       return SCREEN_WIDTH - 18; // 全宽选择器
   }
   ```
4. 在 `s_dispatch` vtable 中添加映射：
   ```c
   static const xerintosh_dispatch_vtable_t s_dispatch[] = {
       // ... 已有条目 ...
       [color_item] = {
           .enter        = dispatch_enter_color,
           .input_next   = dispatch_input_next_color,
           .input_prev   = dispatch_input_prev_color,
           .input_exit   = dispatch_input_exit_default,
           .measure      = dispatch_measure_color,
           .draw         = dispatch_draw_color,
           .draw_overlay = NULL,
           .destroy      = dispatch_destroy_default,
           .has_right_control = dispatch_right_control_true,
       },
   };
   ```
5. 无需修改 `xerintosh_dispatch_*()` 调用方或 `xerintosh_selector_jump_to_selected_item()`

---

## 与其他组件的关系

- **ui_item_selector.c**：通过 `xerintosh_dispatch_enter()`、`xerintosh_dispatch_input_next()`、`xerintosh_dispatch_input_prev()`、`xerintosh_dispatch_input_exit()` 处理所有类型相关的导航行为。
- **ui_core.c**：通过 `xerintosh_dispatch_measure()` 获取选择器宽度；`xerintosh_ui_update_lifecycle()` 仍直接管理 `user_item` 的 init/loop/exit 时序（派发表不介入运行时循环）。
- **ui_draw_list.c**：通过 `xerintosh_dispatch_draw()` 绘制列表项，`xerintosh_dispatch_draw_overlay()` 绘制覆盖层，`xerintosh_dispatch_has_right_control()` 判断文字可用宽度。
- **ui_item_list.c**：通过 `xerintosh_dispatch_destroy()` 调用 `user_item` 的 `destroy_callback`。
- **ui_item.h / ui_item_core.h**：提供 `xerintosh_to_*_item()` 安全类型转换函数与公开派发 API 声明。

---

> **See Also:** [项目系统](item.md) | [核心引擎](core.md) | [全局上下文](context.md)
