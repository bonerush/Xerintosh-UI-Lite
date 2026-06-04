# 项目系统（UI Item）

> **Parent:** [知识地图](../index.md) | **Related:** [核心引擎](core.md), [绘制管线](drawer.md)

## 概述

`ui_item` 是 UI 框架的**数据模型层**，定义了所有可交互元素的类型层次、生命周期和父子关系。采用 **C 风格面向对象**：基类 `xerintosh_list_item_t` 作为结构体第一个成员，派生类通过强制类型转换实现多态。

---

## 关键概念

### 类型层次（继承体系）

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L124-L148)*

```c
typedef enum {
    list_item,    // 普通列表项（可展开子菜单）
    switch_item,  // 开关项
    slider_item,  // 滑条项
    user_item,    // 用户自定义页面
    button_item,  // 按钮项
} xerintosh_list_item_type_t;

// 基类
typedef struct xerintosh_list_item_t {
    xerintosh_list_item_type_t type;
    xerintosh_list_item_icon_t icon;
    const char *content;
    uint8_t layer;
    float y_list_item, y_list_item_trg;   // 当前Y / 目标Y（动画用）
    uint8_t child_num;
    struct xerintosh_list_item_t *child_list_item[MAX_LIST_CHILD_NUM];
    struct xerintosh_list_item_t *parent;
    void *user_data;
    float content_scroll_offset;
    uint32_t scroll_start_time;
    bool is_scrolling;
    const uint8_t *bitmap_data;
    uint8_t bitmap_w;
    uint8_t bitmap_h;
} xerintosh_list_item_t;
```

所有派生类都把 `xerintosh_list_item_t` 作为**第一个成员**：

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L180-L230)*

```c
typedef struct xerintosh_switch_item_t {
    xerintosh_list_item_t base_item;   // 必须放在第一个位置
    bool *value;
    void (*init_function)();
    void (*exit_function)();
} xerintosh_switch_item_t;

typedef struct xerintosh_slider_item_t {
    xerintosh_list_item_t base_item;
    int16_t *value;
    int16_t value_backup;   // 进入编辑时备份原始值
    bool is_confirmed;      // 是否处于编辑状态
    uint8_t value_step;
    int16_t value_max;
    int16_t value_min;
    void (*init_function)();
    void (*exit_function)();
} xerintosh_slider_item_t;

typedef struct xerintosh_user_item_t {
    xerintosh_list_item_t base_item;
    bool in_user_item;        // 是否已进入用户页面
    bool entering_user_item;  // 是否正在播放进入动画
    bool exiting_user_item;   // 是否正在播放退出动画
    void (*init_function)();
    void (*loop_function)();  // 用户页面的每帧回调
    void (*exit_function)();
    bool user_item_inited;
    bool user_item_looping;
} xerintosh_user_item_t;
```

#### 中文伪代码拆解

```
结构体 基类列表项 {
    类型标记
    图标类型
    显示文本
    层级深度
    当前Y坐标, 目标Y坐标     // 动画插值用
    子项数量
    子项指针数组[最大10个]
    父项指针
    用户自定义数据指针
    文字滚动偏移
    滚动开始时间
    是否正在滚动
    自定义位图数据指针
    位图宽度
    位图高度
}

结构体 开关项 {
    基类列表项    // 放在第一个位置，这样 (列表项指针) 和 (开关项指针) 可以安全互转
    布尔值指针    // 指向外部变量
    初始化回调
    退出回调
}

结构体 滑条项 {
    基类列表项
    整数值指针
    原始值备份    // 长按取消时恢复用
    是否已确认    // 短按进入编辑，再短按确认
    步进值
    最大值, 最小值
    初始化回调
    退出回调
}
```

**为什么基类必须放第一位**：C 标准保证结构体的第一个成员偏移量为 0。因此 `(xerintosh_list_item_t*)switch_item_ptr` 是安全的，指针值不变，只是解释方式不同。

### 类型转换函数

*📄 Source: [ui_item_base.c](../../src/ui/ui_item_base.c#L31-L49)*

```c
xerintosh_switch_item_t *xerintosh_to_switch_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_switch_item_t*)xerintosh_safe_cast(_xerintosh_list_item, switch_item);
}
```

每个派生类都有一个安全转换函数。如果类型不匹配，返回 root 作为降级处理，避免空指针崩溃。

### 构造与挂载

*📄 Source: [ui_item_base.c](../../src/ui/ui_item_base.c#L97-L103)*

```c
xerintosh_list_item_t *xerintosh_new_list_item(char *_content, xerintosh_list_item_icon_t icon) {
    xerintosh_list_item_t *_item = malloc(sizeof(xerintosh_list_item_t));
    memset(_item, 0, sizeof(xerintosh_list_item_t));
    _item->type = list_item;
    _item->content = _content;
    if(icon == default_icon)
        _item->icon = list_icon;
    else
        _item->icon = icon;
    return _item;
}
```

#### 中文伪代码拆解

```
函数 新建列表项(文本, 图标) {
    分配内存
    清零结构体        // ESP32 上未初始化变量是随机值，必须清零
    设置类型为列表项
    设置文本
    if (图标是默认图标) {
        使用列表图标
    } else {
        使用指定图标
    }
    return 新项
}
```

### 挂载到树（Push）

*📄 Source: [ui_item_list.c](../../src/ui/ui_item_list.c#L25-L60)*

```c
bool xerintosh_push_item_to_list(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_child) {
    if (_parent == NULL) return false;
    if (_child == NULL) return false;
    if (_parent->child_num >= MAX_LIST_CHILD_NUM) return false;
    if (_parent->layer >= MAX_LIST_LAYER) return false;

    _child->layer = _parent->layer + 1;
    _child->child_num = 0;

    xerintosh_set_font(NULL);
    if (_parent->child_num == 0)
        _child->y_list_item_trg = oled_get_str_height() + LIST_FONT_TOP_MARGIN - 1;
    else
        _child->y_list_item_trg = _parent->child_list_item[_parent->child_num - 1]->y_list_item_trg
                                   + LIST_ITEM_SPACING;

    if (_parent->layer == 0 && _parent->child_num == 0) {
        xerintosh_bind_item_to_selector(_child);
        xerintosh_bind_selector_to_camera(&xerintosh_selector);
    }

    _parent->child_list_item[_parent->child_num++] = _child;
    _child->parent = _parent;
    return true;
}
```

#### 中文伪代码拆解

```
函数 挂载子项到父项(父项, 子项) {
    // 边界检查
    if (父项为空 或 子项为空) return 失败
    if (父项子项数 >= 最大子项数10) return 失败
    if (父项层级 >= 最大层级10) return 失败

    子项.层级 = 父项.层级 + 1
    子项.子项数 = 0

    // 计算子项在列表中的目标Y坐标
    if (父项还没有任何子项) {
        子项.目标Y = 字体高度 + 顶部边距 - 1
    } else {
        上一个兄弟 = 父项.子项数组[末尾]
        子项.目标Y = 上一个兄弟.目标Y + 列表项间距18
    }

    // 根节点的第一个子项自动绑定选择器和相机
    if (父项是根节点 且 是根节点的第一个子项) {
        绑定选择器(子项)
        绑定相机(选择器)
    }

    父项.子项数组[父项子项数++] = 子项
    子项.父项 = 父项
    return 成功
}
```

### 选择器（Selector）

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L369-L375)*

```c
typedef struct xerintosh_selector_t {
    float y_selector, y_selector_trg;
    float w_selector, w_selector_trg;
    float h_selector, h_selector_trg;
    uint8_t selected_index;
    xerintosh_list_item_t *selected_item;
} xerintosh_selector_t;
```

选择器是一个“浮动高亮框”，它有当前坐标（`y/w/h`）和目标坐标（`y_trg/w_trg/h_trg`）。每帧通过 `xerintosh_animation()` 插值实现平滑移动。

### 导航函数

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L71-L104)*

```c
void xerintosh_selector_go_next_item() {
    // 如果当前是滑条且处于编辑模式，改变值而不是移动
    if (xerintosh_selector.selected_item->type == slider_item
        && xerintosh_to_slider_item(xerintosh_selector.selected_item)->is_confirmed) {
        *_selected_slider_item->value += _selected_slider_item->value_step;
        if (*_selected_slider_item->value >= _selected_slider_item->value_max)
            *_selected_slider_item->value = _selected_slider_item->value_max;
        return;
    }

    // 到达末尾时回环到第一个
    if (xerintosh_selector.selected_index == xerintosh_selector.selected_item->parent->child_num - 1) {
        xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent->child_list_item[0];
        xerintosh_selector.selected_index = 0;
        return;
    }

    xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent->child_list_item[++xerintosh_selector.selected_index];
}
```

#### 中文伪代码拆解

```
函数 选择下一个() {
    if (当前项是滑条 且 处于编辑状态) {
        值 += 步进
        if (值超过最大值) 值 = 最大值
        return
    }

    if (已到达当前菜单末尾) {
        选中第一项      // 循环回绕
        索引 = 0
        return
    }

    索引++
    选中同层下一个兄弟项
}
```

### 确认操作（重构后：类型派发）

确认操作在重构后已大幅简化。原来的 ~60 行内联 `if/else if` 链被替换为一行派发调用：

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L147-L151)*

```c
void xerintosh_selector_jump_to_selected_item()
{
    if (!g_in_xerintosh) return;
    xerintosh_dispatch_enter(g_xerintosh_selector.selected_item);
}
```

#### 中文伪代码拆解

```
函数 确认当前选中项() {
    if (不在UI模式) return
    派发确认操作(选择器.选中项)
    // 所有具体的类型逻辑由 ui_dispatch.c 的派发表处理
}
```

实际执行逻辑由 `xerintosh_dispatch_enter()` → `s_enter_dispatch[item->type](item)` 在 O(1) 时间内路由到对应的处理函数。详见 [类型派发表文档](dispatch.md)。

### 回退操作

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L167-L192)*

```c
void xerintosh_selector_go_back_item()
{
    if (!g_in_xerintosh) return;
    // 如果是 user_item，触发退出
    if (g_xerintosh_selector.selected_item->type == user_item) {
        handle_user_item_exit(xerintosh_to_user_item(g_xerintosh_selector.selected_item));
        return;
    }
    // 如果是滑条编辑模式，恢复备份值
    if (g_xerintosh_selector.selected_item->type == slider_item
        && xerintosh_to_slider_item(g_xerintosh_selector.selected_item)->is_confirmed) {
        *_slider->value = _slider->value_backup;
        _slider->is_confirmed = false;
        return;
    }
    // 切换到父项
    if (g_xerintosh_selector.selected_item->parent != NULL) {
        g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->parent;
        g_xerintosh_selector.selected_index = 0;
    }
}
```

### 子项坐标重算（重构新增）

重构中将挂载和移除项时重复的坐标计算逻辑提取为独立函数，消除代码重复。

*📄 Source: [ui_item_list.c](../../src/ui/ui_item_list.c#L16-L28)*

```c
static void recalc_child_y_positions(xerintosh_list_item_t *_parent)
{
    if (_parent == NULL) return;
    xerintosh_set_font(hal_get_cn_font());
    for (uint8_t i = 0; i < _parent->child_num; i++)
    {
        if (i == 0)
            _parent->child_list_item[i]->y_list_item_trg = hal_get_font_height() + LIST_FONT_TOP_MARGIN - 1;
        else
            _parent->child_list_item[i]->y_list_item_trg = _parent->child_list_item[i - 1]->y_list_item_trg + LIST_ITEM_SPACING;
    }
}
```

#### 中文伪代码拆解

```
函数 重算子项Y坐标(父项) {
    if (父项为空) return

    设置字体

    for (遍历父项的所有子项) {
        if (第一个子项) {
            子项.目标Y = 字体高度 + 顶部边距 - 1
        } else {
            子项.目标Y = 上一个子项.目标Y + 列表项间距(18)
        }
    }
}
```

**被以下两处调用**：
- `xerintosh_push_item_to_list()`：挂载新子项后重新计算所有子项坐标
- `xerintosh_remove_item_from_list()`：移除子项后重新计算剩余子项坐标

重构前这两处分別内联了相同的 for 循环，违反 DRY 原则。

### 列表项可见性判断（重构新增公开 API）

`xerintosh_is_item_visible()` 原是 `ui_draw_list.c` 中的 `static` 函数。重构中将其提取为公开 API，方便绘制代码和测试代码复用。

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L96-L102)*

```c
/**
 * @brief  判断列表项 Y 坐标是否在屏幕可视区域内
 * @param  _y_item 项的 y 坐标（屏幕坐标）
 * @return true  可见
 * @return false 不可见（超出上下边界 + 2px 容差）
 */
extern bool xerintosh_is_item_visible(int16_t _y_item);
```

*📄 Source: [ui_draw_list.c](../../src/ui/ui_draw_list.c#L24-L27)*

```c
bool xerintosh_is_item_visible(int16_t _y_item)
{
    return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}
```

**2px 容差**：允许列表项有 2px 的部分超出边界仍被视为"可见"。这避免了列表项在刚好触及边界时被反复裁剪/绘制导致的闪烁。

### 字体缓存修复（重构 Bug 修复）

`xerintosh_set_font()` 原有的缓存逻辑存在 bug——缓存变量 `g_xerintosh_font` 从未被更新，因此每次调用都无条件执行 `hal_set_font()`，缓存形同虚设。

*📄 Source: [ui_item_popup.c](../../src/ui/ui_item_popup.c#L27-L34)*

```c
static const void *g_xerintosh_font = NULL;

void xerintosh_set_font(const void *_font)
{
    if (_font != g_xerintosh_font) {
        g_xerintosh_font = _font;      // ← 修复：之前缺少这一行
        hal_set_font(_font);
    }
}
```

**修复效果**：连续多次 `xerintosh_set_font(同一个字体)` 时，只有第一次会调用 `hal_set_font()`，后续调用被缓存命中跳过，减少了硬件端字体切换开销。

**重要提示**：其他代码中直接调用 `hal_set_font()` 会**绕过**这个缓存。例如 App 使用 `hal_set_font(NULL)` 切换字体后，`g_xerintosh_font` 不会更新，后续 `xerintosh_set_font()` 可能因缓存命中而跳过真正的字体恢复。始终使用 `xerintosh_set_font()` 而非 `hal_set_font()` 来切换字体。

### 相机（Camera / Viewport）

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L424-L432)*

```c
typedef struct xerintosh_camera_t {
    float x_camera, x_camera_trg;
    float y_camera, y_camera_trg;
    xerintosh_selector_t *selector;
} xerintosh_camera_t;
```

相机不是硬件相机，而是**视口偏移量**。当选择器移出屏幕可视区域时，相机会自动调整 `y_camera` 将整个列表向上/向下滚动，确保选择器始终可见。

---

## 与其他组件的关系

- **ui_core**：消费选择器和相机的坐标，每帧调用 `xerintosh_animation()` 进行插值
- **ui_drawer**：读取 `xerintosh_selector.selected_item->parent->child_list_item[]` 绘制列表
- **main.cpp**：调用 `xerintosh_new_*_item()` 和 `xerintosh_push_item_to_list()` 构建菜单树

---

> **See Also:** [核心引擎](core.md) | [绘制管线](drawer.md) | [输入系统](../hal/input.md)
