# 全局上下文系统（UI Context）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [核心引擎](core.md), [绘制管线](drawer.md), [退场动画](draw-anim.md)

## 概述

`ui_context` 是 UI 框架的**全局状态容器**。它使用**单例模式**将所有分散的 UI 全局变量收拢到单一结构体 `xerintosh_context_t` 中，通过 `xerintosh_get_context()` 获取实例，并通过向后兼容宏（如 `g_anim_speed`、`g_in_xerintosh`）让现有代码无感迁移。

在重构前，UI 状态散落在多个文件中：
- `ui_core.c` 中有 `g_anim_speed`、`g_in_xerintosh` 等静态变量
- `ui_draw_anim.c` 中有 4 个 `static` 局部状态变量（`_temp_h`、`_temp_h_trg`、`_last_finished`、`_prev_screen_height`）
- `ui_item_popup.c` 中有 `g_xerintosh_font` 字体缓存
- `ui_draw_list.c` 中有选择器宽度缓存和滚动条长度缓存

重构后，所有这些状态统一由 `xerintosh_context_t` 管理，实现了**单一真实来源**（Single Source of Truth）。

---

## 关键概念

### 上下文结构体

*📄 Source: [ui_context.h](../../src/ui/ui_context.h#L28-L57)*

```c
typedef struct xerintosh_context_t
{
  /* 核心状态 */
  bool in_xerintosh;                  /* UI 是否处于激活状态 */
  bool anim_enabled;                  /* 动画是否启用 */
  bool exit_requested;                /* 外部请求退出当前 user_item */
  uint8_t exit_animation_status;      /* 退场动画阶段状态机 */
  bool exit_animation_finished;       /* 退场动画是否已完成 */
  bool refresh_list_value;            /* 是否需要刷新列表项显示值 */

  /* 绘制状态 */
  uint16_t draw_color;                /* 当前前景色 */
  int16_t anim_speed;                 /* 全局动画速度基准值 */

  /* 选择器宽度缓存（避免每帧重复 hal_get_string_width） */
  const char *cached_selector_content;    /* 上次缓存时选中的内容指针 */
  int16_t cached_selector_text_width;     /* 上次缓存时测得的文字宽度 */

  /* 退场动画状态（从 ui_draw_anim.c 的 static 变量迁移） */
  float exit_anim_temp_h;              /* 遮罩当前高度 */
  float exit_anim_temp_h_trg;          /* 遮罩目标高度 */
  bool exit_anim_last_finished;        /* 上一帧的 finished 状态 */
  int16_t exit_anim_prev_screen_h;     /* 上一帧的屏幕高度（方向切换检测） */

  /* 子系统状态（指针，指向 ui_context.c 内部存储） */
  struct xerintosh_selector_t *selector;
  struct xerintosh_camera_t *camera;
  struct xerintosh_info_bar_t *info_bar;
  struct xerintosh_pop_up_t *pop_up;
} xerintosh_context_t;
```

#### 字段详解

| 字段 | 类型 | 初始值 | 说明 |
|------|------|--------|------|
| `in_xerintosh` | `bool` | `true` | 全局 UI 激活标志，设为 false 后主循环退出 |
| `anim_enabled` | `bool` | `true` | 全局动画开关，关闭后所有位置直接跳变 |
| `exit_requested` | `bool` | `false` | 外部组件请求退出当前 user_item 的信号 |
| `exit_animation_status` | `uint8_t` | `0` | 退场动画状态机：0=展开中, 1=到达底部, 2=回缩中 |
| `exit_animation_finished` | `bool` | `true` | 退场动画是否已完成 |
| `refresh_list_value` | `bool` | `true` | 标记需要重新调用 switch/slider 的 init_function |
| `draw_color` | `uint16_t` | `0xFFFF` | 当前前景色（白色） |
| `anim_speed` | `int16_t` | `92` | 全局动画速度基准值，越大越快 |
| `cached_selector_content` | `const char *` | `NULL` | 上次测量宽度时对应的内容字符串指针 |
| `cached_selector_text_width` | `int16_t` | `0` | 缓存的文字宽度值（px） |
| `exit_anim_temp_h` | `float` | `-8` | 退场遮罩当前高度 |
| `exit_anim_temp_h_trg` | `float` | `-999` | 退场遮罩目标高度（-999 表示未设置目标） |
| `exit_anim_last_finished` | `bool` | `true` | 上一帧动画是否完成（用于检测新动画开始） |
| `exit_anim_prev_screen_h` | `int16_t` | `-1` | 上一帧屏幕高度（-1 强制首次检测后更新） |

### 单例模式

*📄 Source: [ui_context.c](../../src/ui/ui_context.c#L24-L35)*

```c
static xerintosh_context_t g_ui_ctx = {
  .in_xerintosh = true,
  .anim_enabled = true,
  .exit_animation_finished = true,
  .refresh_list_value = true,
  .draw_color = 0xFFFF,
  .anim_speed = 92,
  .cached_selector_content = NULL,
  .cached_selector_text_width = 0,
  .exit_anim_temp_h = -8,
  .exit_anim_temp_h_trg = -999,
  .exit_anim_last_finished = true,
  .exit_anim_prev_screen_h = -1,
  .selector = &s_selector,
  .camera = &s_camera,
  .info_bar = &s_info_bar,
  .pop_up = &s_pop_up,
};
```

使用**静态全局变量** + 指定初始化器在编译期完成初始化，无需动态分配，适合嵌入式环境。

```c
xerintosh_context_t *xerintosh_get_context(void)
{
    return &g_ui_ctx;
}
```

始终返回同一个全局实例的指针，永不返回 NULL。

### 初始化函数

*📄 Source: [ui_context.c](../../src/ui/ui_context.c#L47-L68)*

```c
void xerintosh_context_init(void)
{
    g_ui_ctx.in_xerintosh = true;
    g_ui_ctx.anim_enabled = true;
    // ... 重置所有字段 ...
    s_selector = (xerintosh_selector_t){};
    s_camera = (xerintosh_camera_t){};
    s_info_bar = (xerintosh_info_bar_t){};
    s_pop_up = (xerintosh_pop_up_t){};
}
```

提供显式的初始化函数，用于：
1. **测试隔离**：每个测试用例开始前重置上下文
2. **UI 热重启**：从 user_item 异常退出后重新初始化

区别于静态初始化，`xerintosh_context_init()` 还会将子系统状态（selector、camera 等）通过 `= {}` 清零。

### 向后兼容宏

*📄 Source: [ui_context.h](../../src/ui/ui_context.h#L76-L89)*

```c
#define g_in_xerintosh                       (xerintosh_get_context()->in_xerintosh)
#define g_anim_enabled                       (xerintosh_get_context()->anim_enabled)
#define g_xerintosh_exit_requested           (xerintosh_get_context()->exit_requested)
#define g_xerintosh_exit_animation_status    (xerintosh_get_context()->exit_animation_status)
#define g_xerintosh_exit_animation_finished  (xerintosh_get_context()->exit_animation_finished)
#define g_xerintosh_refresh_list_value       (xerintosh_get_context()->refresh_list_value)
#define g_xerintosh_draw_color               (xerintosh_get_context()->draw_color)
#define g_anim_speed                         (xerintosh_get_context()->anim_speed)
#define g_xerintosh_cached_selector_content  (xerintosh_get_context()->cached_selector_content)
#define g_xerintosh_cached_selector_width    (xerintosh_get_context()->cached_selector_text_width)
#define g_xerintosh_exit_anim_temp_h         (xerintosh_get_context()->exit_anim_temp_h)
#define g_xerintosh_exit_anim_temp_h_trg     (xerintosh_get_context()->exit_anim_temp_h_trg)
#define g_xerintosh_exit_anim_last_finished  (xerintosh_get_context()->exit_anim_last_finished)
#define g_xerintosh_exit_anim_prev_screen_h  (xerintosh_get_context()->exit_anim_prev_screen_h)
```

#### 中文伪代码拆解

```
宏 g_anim_speed 展开为:
    ↓
xerintosh_get_context()        // 获取单例指针
    ->anim_speed               // 访问成员字段

// 等价于读写一个全局变量，但背后通过指针间接访问
// 读取: int16_t s = g_anim_speed;
// 写入: g_anim_speed = 90;
```

**设计考量**：
- 宏展开包含函数调用 `xerintosh_get_context()`，每次访问都会调用一次 —— 但该函数仅返回一个静态指针，编译器通常会内联优化
- 宏不是左值（lvalue），但展开后的表达式 `(ptr->field)` 是可赋值的左值，因此 `g_anim_speed = 90` 是合法的
- 所有宏名以 `g_` 开头，模仿全局变量命名风格，对现有代码透明

---

## 退场动画状态迁移（Task 9 重构）

### 重构前：文件局部 static 变量

在 `ui_draw_anim.c` 中，退场动画使用 4 个 `static` 局部变量：

```c
void xerintosh_draw_exit_animation()
{
  static float _temp_h = -8;               // ← 问题：测试中无法重置
  static float _temp_h_trg = -999;
  static bool _last_finished = true;
  static int16_t _prev_screen_height = -1;
  // ...
}
```

**问题**：
1. **测试隔离困难**：`static` 变量在函数第一次调用后持续存在，无法通过 `xerintosh_context_init()` 重置
2. **状态不可见**：调试时无法检查这些内部状态的值
3. **不符合架构**：与"所有 UI 状态集中在 context" 的设计不一致

### 重构后：上下文字段

```c
void xerintosh_draw_exit_animation()
{
  // 不再使用 static 变量，改为使用上下文宏
  g_xerintosh_exit_anim_temp_h
  g_xerintosh_exit_anim_temp_h_trg
  g_xerintosh_exit_anim_last_finished
  g_xerintosh_exit_anim_prev_screen_h
  // ...
}
```

**收益**：
1. `xerintosh_context_init()` 可以统一重置所有 UI 状态
2. 调试时可以通过检查 `g_ui_ctx` 查看退场动画内部状态
3. 架构一致性：所有全局状态集中管理

### 屏幕方向切换防护

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L31-L37)*

```c
if (g_xerintosh_exit_anim_prev_screen_h != SCREEN_HEIGHT)
{
    float max_h = SCREEN_HEIGHT + 8;
    if (g_xerintosh_exit_anim_temp_h > max_h) g_xerintosh_exit_anim_temp_h = max_h;
    if (g_xerintosh_exit_animation_status == 0) g_xerintosh_exit_anim_temp_h_trg = max_h;
    g_xerintosh_exit_anim_prev_screen_h = SCREEN_HEIGHT;
}
```

当屏幕旋转（横屏 ↔ 竖屏）时，`SCREEN_HEIGHT` 从 80 变为 160（或反之）。如果不检测这种变化，旧的 `exit_anim_temp_h_trg` 可能指向一个不存在的屏幕高度（如横屏时 `temp_h_trg = 80+8 = 88`，旋转到竖屏后正在展开的遮罩在 88px 处就错误地触发"到达底部"的判断）。此检测在每次方向切换时重置目标高度。

---

## 性能缓存

### 选择器宽度缓存

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L137-L143)*

```c
if (g_xerintosh_cached_selector_content != g_xerintosh_selector.selected_item->content) {
    g_xerintosh_cached_selector_content = g_xerintosh_selector.selected_item->content;
    g_xerintosh_cached_selector_width = hal_get_string_width(g_xerintosh_selector.selected_item->content);
}
g_xerintosh_selector.w_selector_trg = g_xerintosh_cached_selector_width + 12;
```

**优化原理**：`hal_get_string_width()` 需要遍历 UTF-8 字符串并查询字体表，是每帧最昂贵的操作之一。在 99% 的帧中，选中项没有变化（用户没有按按钮），但旧代码每帧都重新测量。缓存将"内容指针比较"（1 次指针解引用）替代"字符串宽度测量"（N 次字符查询）。

---

## 与其他组件的关系

- **ui_core.c**：读写 `in_xerintosh`、`anim_speed`、`exit_animation_*` 等核心状态
- **ui_draw_anim.c**：读写退场动画的 4 个状态字段
- **ui_item_popup.c**：有独立的 `g_xerintosh_font` 字体缓存（尚未迁移到 context，属于下一个重构周期）
- **ui_draw_list.c**：有独立的滚动条长度缓存（尚未迁移）
- **测试代码**：通过 `xerintosh_context_init()` 在测试用例间重置状态

---

> **See Also:** [核心引擎](core.md) | [退场动画](draw-anim.md) | [绘制管线](drawer.md)
