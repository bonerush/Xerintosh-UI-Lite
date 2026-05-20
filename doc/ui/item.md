# 项目系统（UI Item）

> **Parent:** [知识地图](../index.md) | **Related:** [核心引擎](core.md), [绘制管线](drawer.md), [绘制驱动适配](draw-driver.md)

## 概述

`ui_item` 是 UI 框架的**数据模型层**，定义了所有可交互元素的类型层次、生命周期和父子关系。采用 **C 风格面向对象**：基类 `astra_list_item_t` 作为结构体第一个成员，派生类通过强制类型转换实现多态。

---

## 关键概念

### 类型层次（继承体系）

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L82-L106)*

```c
typedef enum {
    list_item,    // 普通列表项（可展开子菜单）
    switch_item,  // 开关项
    slider_item,  // 滑条项
    user_item,    // 用户自定义页面
    button_item,  // 按钮项
} astra_list_item_type_t;

// 基类
typedef struct astra_list_item_t {
    astra_list_item_type_t type;
    astra_list_item_icon_t icon;
    char *content;
    uint8_t layer;
    float y_list_item, y_list_item_trg;   // 当前Y / 目标Y（动画用）
    uint8_t child_num;
    struct astra_list_item_t *child_list_item[MAX_LIST_CHILD_NUM];
    struct astra_list_item_t *parent;
} astra_list_item_t;
```

所有派生类都把 `astra_list_item_t` 作为**第一个成员**：

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L117-L151)*

```c
typedef struct astra_switch_item_t {
    astra_list_item_t base_item;   // 必须放在第一个位置
    bool *value;
    void (*init_function)();
    void (*exit_function)();
} astra_switch_item_t;

typedef struct astra_slider_item_t {
    astra_list_item_t base_item;
    int16_t *value;
    int16_t value_backup;   // 进入编辑时备份原始值
    bool is_confirmed;      // 是否处于编辑状态
    uint8_t value_step;
    int16_t value_max;
    int16_t value_min;
    void (*init_function)();
    void (*exit_function)();
} astra_slider_item_t;

typedef struct astra_user_item_t {
    astra_list_item_t base_item;
    bool in_user_item;        // 是否已进入用户页面
    bool entering_user_item;  // 是否正在播放进入动画
    bool exiting_user_item;   // 是否正在播放退出动画
    void (*init_function)();
    void (*loop_function)();  // 用户页面的每帧回调
    void (*exit_function)();
    bool user_item_inited;
    bool user_item_looping;
} astra_user_item_t;
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

**为什么基类必须放第一位**：C 标准保证结构体的第一个成员偏移量为 0。因此 `(astra_list_item_t*)switch_item_ptr` 是安全的，指针值不变，只是解释方式不同。

### 类型转换函数

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L60-L90)*

```c
astra_switch_item_t *astra_to_switch_item(astra_list_item_t *_item) {
    if (_item != NULL && _item->type == switch_item)
        return (astra_switch_item_t*)_item;
    return (astra_switch_item_t*)astra_get_root_list();
}
```

每个派生类都有一个安全转换函数。如果类型不匹配，返回 root 作为降级处理，避免空指针崩溃。

### 构造与挂载

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L106-L135)*

```c
astra_list_item_t *astra_new_list_item(char *_content, astra_list_item_icon_t icon) {
    astra_list_item_t *_item = malloc(sizeof(astra_list_item_t));
    memset(_item, 0, sizeof(astra_list_item_t));
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

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L397-L421)*

```c
bool astra_push_item_to_list(astra_list_item_t *_parent, astra_list_item_t *_child) {
    if (_parent == NULL) return false;
    if (_child == NULL) return false;
    if (_parent->child_num >= MAX_LIST_CHILD_NUM) return false;
    if (_parent->layer >= MAX_LIST_LAYER) return false;

    _child->layer = _parent->layer + 1;
    _child->child_num = 0;

    astra_set_font(NULL);
    if (_parent->child_num == 0)
        _child->y_list_item_trg = oled_get_str_height() + LIST_FONT_TOP_MARGIN - 1;
    else
        _child->y_list_item_trg = _parent->child_list_item[_parent->child_num - 1]->y_list_item_trg
                                   + LIST_ITEM_SPACING;

    if (_parent->layer == 0 && _parent->child_num == 0) {
        astra_bind_item_to_selector(_child);
        astra_bind_selector_to_camera(&astra_selector);
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

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L189-L204)*

```c
typedef struct astra_selector_t {
    float y_selector, y_selector_trg;
    float w_selector, w_selector_trg;
    float h_selector, h_selector_trg;
    uint8_t selected_index;
    astra_list_item_t *selected_item;
} astra_selector_t;
```

选择器是一个“浮动高亮框”，它有当前坐标（`y/w/h`）和目标坐标（`y_trg/w_trg/h_trg`）。每帧通过 `astra_animation()` 插值实现平滑移动。

### 导航函数

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L226-L274)*

```c
void astra_selector_go_next_item() {
    // 如果当前是滑条且处于编辑模式，改变值而不是移动
    if (astra_selector.selected_item->type == slider_item
        && astra_to_slider_item(astra_selector.selected_item)->is_confirmed) {
        *_selected_slider_item->value += _selected_slider_item->value_step;
        if (*_selected_slider_item->value >= _selected_slider_item->value_max)
            *_selected_slider_item->value = _selected_slider_item->value_max;
        return;
    }

    // 到达末尾时回环到第一个
    if (astra_selector.selected_index == astra_selector.selected_item->parent->child_num - 1) {
        astra_selector.selected_item = astra_selector.selected_item->parent->child_list_item[0];
        astra_selector.selected_index = 0;
        return;
    }

    astra_selector.selected_item = astra_selector.selected_item->parent->child_list_item[++astra_selector.selected_index];
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

### 确认与回退

*📄 Source: [ui_item.c](../../src/ui/ui_item.c#L282-L345)*

```c
void astra_selector_jump_to_selected_item() {
    if (!in_astra) return;

    if (astra_selector.selected_item->type == user_item) {
        // 进入用户自定义页面
        astra_exit_animation_finished = false;
        _selected_user_item->entering_user_item = true;
        return;
    }

    if (astra_selector.selected_item->type == switch_item) {
        *_selected_switch_item->value = !*_selected_switch_item->value;
        if (_selected_switch_item->exit_function)
            _selected_switch_item->exit_function();
        return;
    }

    if (astra_selector.selected_item->type == slider_item) {
        if (!_selected_slider_item->is_confirmed) {
            _selected_slider_item->is_confirmed = true;
            _selected_slider_item->value_backup = *_selected_slider_item->value;
            return;
        }
        if (_selected_slider_item->is_confirmed) {
            if (_selected_slider_item->exit_function)
                _selected_slider_item->exit_function();
            _selected_slider_item->is_confirmed = false;
            return;
        }
    }

    // 普通列表项：进入子菜单
    if (astra_selector.selected_item->child_num == 0) return;
    // ... 将选中项切到第一个子项
}
```

#### 中文伪代码拆解

```
函数 确认当前选中项() {
    if (当前项是用户页面) {
        播放退场动画
        标记为正在进入用户页面
        return
    }

    if (当前项是开关) {
        值 = !值          // 翻转布尔值
        执行退出回调（副作用）
        return
    }

    if (当前项是滑条) {
        if (还没进入编辑模式) {
            标记为已确认
            备份当前值       // 长按取消时恢复用
            return
        } else {
            执行退出回调
            取消确认标记      // 退出编辑模式
            return
        }
    }

    // 普通列表项：进入子菜单
    if (没有子项) return
    选中索引 = 0
    当前项 = 第一个子项
}
```

### 相机（Camera / Viewport）

*📄 Source: [ui_item.h](../../src/ui/ui_item.h#L206-L216)*

```c
typedef struct astra_camera_t {
    float x_camera, x_camera_trg;
    float y_camera, y_camera_trg;
    astra_selector_t *selector;
} astra_camera_t;
```

相机不是硬件相机，而是**视口偏移量**。当选择器移出屏幕可视区域时，相机会自动调整 `y_camera` 将整个列表向上/向下滚动，确保选择器始终可见。

---

## 与其他组件的关系

- **ui_core**：消费选择器和相机的坐标，每帧调用 `astra_animation()` 进行插值
- **ui_drawer**：读取 `astra_selector.selected_item->parent->child_list_item[]` 绘制列表
- **main.cpp**：调用 `astra_new_*_item()` 和 `astra_push_item_to_list()` 构建菜单树

---

> **See Also:** [核心引擎](core.md) | [绘制管线](drawer.md) | [输入系统](../hal/input.md)
