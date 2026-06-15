# UI 脏矩形管理

> **Parent:** [UI 核心层索引](index.md) | **Source:** [ui_dirty.h](../../src/ui/ui_dirty.h), [ui_dirty.c](../../src/ui/ui_dirty.c)
>
> 提供统一的脏矩形标记 API，用于控制 UI 渲染管线的局部刷新优化。

---

## 概述

`xerintosh_invalidate()` 是 App 开发者标记 UI 需要重绘的**唯一推荐入口**。框架在渲染完成后自动调用 `xerintosh_clear_dirty()` 清除标志。

```
xerintosh_invalidate()          ← App 开发者 / 框架自动
        │
        ▼
  g_ui_ctx.dirty = true
        │
        ▼
  下一帧检测 dirty == true
        │
        ▼
  hal_display_clear() → 全量渲染
        │
        ▼
  xerintosh_clear_dirty()        ← 框架自动
```

*📄 Source: [ui_dirty.c](../../src/ui/ui_dirty.c#L18-L44)*

---

## API 参考

### 核心 API

| 函数 | 签名 | 调用者 | 说明 |
|------|------|--------|------|
| `xerintosh_invalidate()` | `(void) → void` | **App 开发者 + 框架** | 标记 UI 脏状态，请求下一帧重绘 |
| `xerintosh_is_dirty()` | `(void) → bool` | 框架内部 | 查询当前脏状态 |
| `xerintosh_clear_dirty()` | `(void) → void` | 框架内部 | 清除脏标志（渲染完成后调用） |

### 向后兼容

| 函数 | 说明 |
|------|------|
| `xerintosh_mark_dirty()` | `xerintosh_invalidate()` 的别名，已弃用 |

*📄 Source: [ui_dirty.h](../../src/ui/ui_dirty.h#L27-L65)*

---

## 使用场景

### App 开发者场景

在菜单模式下的非动画代码中，当需要即时显示 UI 状态变化时调用：

```c
/* 例：WiFi 状态回调 */
void on_wifi_status_changed(void *user_data) {
    (void)user_data;
    update_status_icon();
    xerintosh_invalidate();  // 强制下一帧刷新
}
```

*📄 Source: [ui_dirty.h](../../src/ui/ui_dirty.h#L36-L42)*

### 框架自动 invalidate 的场景

以下场景**自动**调用 `xerintosh_invalidate()`，开发者无需手动调用：

| 场景 | 触发位置 | 说明 |
|------|----------|------|
| 按键导航 | `ui_item_selector.c` | 选择器移动/确认/返回 |
| 动画播放 | `ui_core.c` | 选择器/相机/退场动画进行中 |
| 文字滚动 | `ui_draw_list.c` | 选中项名称跑马灯 |
| 生命周期变更 | `ui_dispatch.c` | 进入/退出 user_item、slider 编辑模式 |
| 类型派发 | `ui_dispatch.c` | enter / input_next / input_prev / input_exit |

*📄 Source: [ui_item_selector.c](../../src/ui/ui_item_selector.c#L56), [ui_core.c](../../src/ui/ui_core.c#L226)*

---

## 内部实现

脏标志存储在单例上下文 `xerintosh_context_t` 的 `dirty` 字段中：

*📄 Source: [ui_context.h](../../src/ui/ui_context.h#L39)*

```c
// ui_context.h — 单例结构体中的 dirty 字段
typedef struct {
    // ...
    bool dirty;  /* 脏矩形标志 */
    // ...
} xerintosh_context_t;
```

渲染管线在每帧检查这个标志，决定是否清屏：

*📄 Source: [ui_task.c](../../src/ui/ui_task.c#L56-L58)*

```c
if (xerintosh_is_in_user_item() || xerintosh_is_dirty()) {
    hal_display_clear();
}
```

---

## 兼容性

- `g_xerintosh_dirty` 宏保留为废弃别名，指向 `xerintosh_get_context()->dirty`
- 新建代码**必须**使用 `xerintosh_invalidate()` / `xerintosh_is_dirty()` / `xerintosh_clear_dirty()`
- `xerintosh_mark_dirty()` 标记为 `@deprecated`，但通过 inline 转发保持兼容

*📄 Source: [ui_context.h](../../src/ui/ui_context.h#L83)*
