# UI 核心层索引

> **Parent:** [知识地图](../index.md)

## 概述

UI 核心层是 Xerintosh 菜单框架的主体，纯 C 实现。它提供菜单数据模型、动画引擎、渲染管线、类型派发和全局状态管理。

在 2026 年 6 月的深度重构（12 次提交）中，本层经历了系统性的可维护性、性能和架构改进。

## 模块列表

| 模块 | 文档 | 源码 | 说明 |
|------|------|------|------|
| 项目系统 | [item.md](item.md) | `src/ui/ui_item.h`, `ui_item_*.c` | 菜单树数据模型、五种 Item 类型、选择器与相机 |
| 核心引擎 | [core.md](core.md) | `src/ui/ui_core.c/h` | 动画插值、生命周期管理、主循环调度、渲染分支 |
| 类型派发表 | [dispatch.md](dispatch.md) | `src/ui/ui_dispatch.c` | 函数指针数组替代内联 switch，O(1) 类型路由 |
| 全局上下文 | [context.md](context.md) | `src/ui/ui_context.c/h` | 单例状态容器、向后兼容宏、退场动画状态迁移 |
| 绘制管线 | [drawer.md](drawer.md) | `src/ui/ui_draw_*.c`, `ui_drawer.h` | 列表外观、选择器 XOR 高亮、弹窗与信息栏、退场动画、图标 |
| 行列表动画 | [ui-anim-row.md](ui-anim-row.md) | `src/ui/ui_anim_row.c/h` | 可复用行列表动画工具（入场滑入 + 高亮平滑过渡） |
| 绘制驱动适配 | [draw-driver.md](draw-driver.md) | ⚠️ 已移除 | 原 `oled_*` → `hal_*` 宏桥接层，已在重构中删除 |

## 重构概述（2026 年 6 月）

| Task | 提交 | 变更概要 |
|------|------|----------|
| T1 | `ed17fb9` | 提取 `xerintosh_is_item_visible()` 为公开 API |
| T2 | `91a756e` | 提取 `recalc_child_y_positions()` 消除重复坐标计算 |
| T3 | `f86810d` | 修复 `xerintosh_set_font()` 缓存（之前从未更新缓存变量） |
| T4 | `6d937ca` | 删除 `ui_draw_driver.c/h` 死代码（薄包装层无价值） |
| T5 | `c268fb4` | 缓存滚动条段长，仅 `child_num` 变化时重新计算 |
| T6 | `7e67c32` | 缓存选择器字符串宽度，避免每帧 `hal_get_string_width()` |
| T7 | `f58701f` | 创建 `ui_dispatch.c` 类型派发表，替代内联 switch |
| T8 | `e723bb2` | 拆分主循环为 `xerintosh_ui_update_lifecycle()` + `xerintosh_ui_render_frame()` |
| T9 | `2f3dad1` | 迁移退场动画 static 状态到 `ui_context` |
| T10 | `26af50f` | 添加 `xerintosh_is_item_visible()` 边界回归测试 |

## 渲染管线（每帧顺序）

1. `hal_display_clear()` — 清除后台缓冲区
2. `xerintosh_ui_main_core()` → `xerintosh_ui_update_lifecycle()` — user_item 生命周期
3. `xerintosh_ui_main_core()` → `xerintosh_ui_render_frame()` — 列表模式渲染
   - `xerintosh_refresh_camera_position()` — 相机视口
   - `xerintosh_refresh_list_item_position()` — 列表项 Y 坐标插值
   - `xerintosh_refresh_selector_position()` — 选择器 Y/W/H 插值
   - `xerintosh_draw_list()` — 列表外观 + 列表项 + 选择器高亮
4. `xerintosh_draw_exit_animation()` — 退场遮罩（沙漏 + 扫描线）
5. `xerintosh_ui_widget_core()` — Widget（信息栏 + 弹窗）叠加层
6. `hal_display_flush()` — DMA `pushSprite` 刷新到屏幕

---

> **See Also:** [App 层](../app/index.md) | [HAL 层](../hal/index.md) | [内核层](../kernel/index.md)
