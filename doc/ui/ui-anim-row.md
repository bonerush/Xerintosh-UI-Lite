# 行列表动画工具（ui_anim_row）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [核心引擎](core.md), [项目系统](item.md), [绘制管线](drawer.md)

## 概述

`ui_anim_row` 是一个**可复用的行列表动画工具**，将 UI 核心引擎的 `xerintosh_animation()` 缓动函数封装为面向行列表场景的高层 API。所有需要"行列表 + 高亮选择器"的 App（如任务管理器）只需引入本模块，不再重复实现动画循环。

设计理念：
- **最小侵入**：不修改现有 UI 框架代码，作为独立模块叠加使用
- **复用优先**：任务管理器、串口监视器和未来所有行列表 App 共享同一套动画逻辑
- **与菜单引擎一致**：使用相同的缓动公式和速度分级常量

*📄 Source: [ui_anim_row.h](../../src/ui/ui_anim_row.h), [ui_anim_row.c](../../src/ui/ui_anim_row.c)*

---

## 关键概念

### 数据结构

#### 单行动画状态 — `xerintosh_anim_row_t`

```c
typedef struct {
    float y;         // 当前动画 Y 坐标
    float y_trg;     // 目标 Y 坐标
    float w;         // 当前动画宽度（高亮框）
    float w_trg;     // 目标宽度
} xerintosh_anim_row_t;
```

每行独立维护一对 `(当前值, 目标值)`，由 `xerintosh_animation()` 每帧驱动 `当前值` 向 `目标值` 平滑逼近。

#### 列表动画上下文 — `xerintosh_anim_row_list_t`

```c
#define ANIM_ROW_MAX 10

typedef struct {
    xerintosh_anim_row_t rows[ANIM_ROW_MAX];  // 各行动画状态
    xerintosh_anim_row_t highlight;           // 高亮框动画状态
    float   scroll_offset;                    // 当前滚动偏移
    float   scroll_offset_trg;                // 目标滚动偏移
    int     visible_count;                    // 可见行数
    int16_t row_height;                       // 每行高度（含间距）
    int16_t list_top;                         // 列表区域顶部 Y 坐标
} xerintosh_anim_row_list_t;
```

整个列表的动画上下文，包含：
- **rows[]**：N 个可见行的动画状态（N = `visible_count`）
- **highlight**：选中高亮框的动画状态（Y 坐标 + 宽度）
- **scroll_offset**：列表滚动的平滑偏移（消除整数滚动的跳动感）
- 布局参数：行高、列表顶部坐标

### API 函数

| 函数 | 说明 | 调用时机 |
|------|------|----------|
| `xerintosh_anim_row_list_init(list, count, row_h, top)` | 初始化上下文，所有行起点设为屏幕底部 | App `init()` 中 |
| `xerintosh_anim_row_list_refresh(list, sel, scroll, sw, count)` | 重新计算所有目标位置 | `selected` 或 `scroll` 变化时 |
| `xerintosh_anim_row_list_update(list, speed)` | 每帧更新动画，返回 `true` 表示已稳定 | 每帧绘制前 |

#### 中文伪代码拆解

```
函数 初始化动画上下文(列表, 可见行数, 行高, 列表顶部Y) {
    保存布局参数（行高、列表顶部Y）
    for (i = 0; i < 可见行数; i++) {
        第i行.当前Y = 屏幕高度    // 所有行从屏幕底部开始
        第i行.目标Y = 列表顶部Y + i * 行高  // 最终位置
    }
    高亮框.当前Y = 屏幕高度       // 高亮也从底部滑入
    高亮框.当前W = 屏幕宽度       // 高亮宽度初始化为全宽
}

函数 每帧更新动画(列表, 速度) {
    全部稳定 = true
    for (i = 0; i < 可见行数; i++) {
        调用缓动(第i行.当前Y, 第i行.目标Y, 速度)
        if (当前Y 和 目标Y 差距 > 0.5px) 全部稳定 = false
    }
    调用缓动(高亮框.当前Y, 高亮框.目标Y, 速度)
    调用缓动(高亮框.当前W, 高亮框.目标W, 速度)
    调用缓动(滚动偏移.当前, 滚动偏移.目标, 速度)
    return 全部稳定
}

函数 刷新目标位置(列表, 选中索引, 滚动偏移, 屏幕宽度, 总项目数) {
    for (i = 0; i < 可见行数; i++) {
        idx = i + 滚动偏移
        if (idx < 总项目数) {
            第i行.目标Y = 正常位置    // 项目存在，正常排列
        } else {
            第i行.目标Y = 视野外       // 超越列表尾部，向下推出
        }
    }
    高亮框.目标Y = 选中行.目标Y       // 高亮框跟随选中行
    高亮框.目标W = 屏幕宽度            // 高亮宽度匹配屏幕
}
```

关键设计：`init()` 设置**目标位置**，`refresh()` **修改**目标位置，`update()` 将**当前值**推向目标——三者职责分离，干净清晰。

---

## 使用模式

### 模式 1：集成到 App 状态

```c
// myapp.h
typedef struct {
    int       selected;
    int       scroll;
    int       item_count;
    xerintosh_anim_row_list_t anim_list;  // ← 嵌入动画上下文
} myapp_state_t;

// myapp.c — init()
void myapp_init(void) {
    int visible = calc_visible_lines();
    xerintosh_anim_row_list_init(&g_state.anim_list, visible, row_h, list_top);
}

// myapp.c — loop()
void myapp_loop(void) {
    // 输入处理...
    if (selected_changed || scroll_changed) {
        xerintosh_anim_row_list_refresh(&g_state.anim_list,
            g_state.selected, g_state.scroll, SCREEN_WIDTH, g_state.item_count);
    }
    xerintosh_anim_row_list_update(&g_state.anim_list, ANIM_SPEED_SELECTOR);
    myapp_draw();
}

// myapp_ui.c — draw()
void myapp_draw(void) {
    for (int i = 0; i < visible; i++) {
        int16_t row_y = (int16_t)g_state.anim_list.rows[i].y;  // ← 动画 Y
        // 使用 row_y 绘制每一行...
    }
    // 使用 g_state.anim_list.highlight.y/w 绘制高亮框
}
```

### 模式 2：入场动画

`init()` 将 `rows[i].y` 的初始值设为 `SCREEN_HEIGHT`（屏幕底部），目标值设为最终行位置。`update()` 每帧将当前 Y 推向目标 Y，产生 **"行从底部滑入"** 的入场效果。动画时长由 `ANIM_SPEED_EXIT`（~250ms）或 `ANIM_SPEED_SELECTOR`（~150ms）控制。

### 模式 3：选中切换动画

`refresh()` 将 `highlight.y_trg` 设为新选中行的 `y_trg`。`update()` 驱动 `highlight.y` 平滑过渡，效果是从旧行位置滑到新行位置，不会出现"高亮框跳动"。

---

## 与菜单引擎的关系

| 菜单引擎原语 | ui_anim_row 等价物 | 说明 |
|-------------|-------------------|------|
| `xerintosh_animation()` | 内部调用同一函数 | 共用缓动核心 |
| `xerintosh_refresh_list_item_position()` | `xerintosh_anim_row_list_update()` | 逐行驱动动画 |
| `xerintosh_refresh_selector_position()` | `highlight.y_trg` 更新 + `update()` | 高亮框动画 |
| `y_camera` → `y_camera_trg` | `scroll_offset` → `scroll_offset_trg` | 滚动平滑 |

`ui_anim_row` 不替代菜单引擎，而是将其**行列表动画能力**提取为独立模块，供不需要完整菜单树（如任务管理器、日志查看器）的场景使用。

---

## 设计约束

- **最大行数**：`ANIM_ROW_MAX = 10`（M5Stick-C 80×160 屏幕最多同时显示 ~10 行）
- **浮点精度**：使用 `float`，快照阈值为 `0.5f`（低于此值视为已稳定）
- **速度分级**：复用 UI 核心层的 `ANIM_SPEED_SELECTOR` / `ANIM_SPEED_EXIT` 等常量
- **调用顺序**：必须遵循 `init() → refresh() → update()` 的顺序

---

> **See Also:** [核心引擎](core.md) | [项目系统](item.md) | [绘制管线](drawer.md) | [任务管理器](../app/taskmgr.md)
