# UI 核心层索引

> **Parent:** [知识地图](../index.md)

## 概述

UI 核心层是 Xerintosh 菜单框架的主体，纯 C 实现。它提供菜单数据模型、动画引擎、渲染管线和绘制驱动适配。

## 模块列表

| 模块 | 文档 | 源码 | 说明 |
|------|------|------|------|
| 绘制驱动适配 | [draw-driver.md](draw-driver.md) | `src/ui/ui_draw_driver.c/h` | `oled_*` 宏 → `hal_*` 的宏桥接层 |
| 项目系统 | [item.md](item.md) | `src/ui/ui_item.c/h` | 菜单树数据模型、五种 Item 类型、选择器与相机 |
| 核心引擎 | [core.md](core.md) | `src/ui/ui_core.c/h` | 动画插值、相机滚动、主循环调度 |
| 绘制管线 | [drawer.md](drawer.md) | `src/ui/ui_drawer.c/h` | 列表外观、选择器 XOR 高亮、弹窗与信息栏 |
| 行列表动画 | [ui-anim-row.md](ui-anim-row.md) | `src/ui/ui_anim_row.c/h` | 可复用行列表动画工具（入场滑入 + 高亮平滑过渡） |

## 渲染管线（每帧顺序）

1. `hal_display_clear()` — 清除后台缓冲区
2. `xerintosh_draw_list_item()` — 绘制背景层（列表项、控件）
3. `xerintosh_draw_selector()` — XOR 反色选择器高亮
4. `xerintosh_draw_widget()` — 弹窗/信息栏叠加层
5. `hal_display_flush()` — DMA `pushSprite` 刷新到屏幕

---

> **See Also:** [App 层](../app/index.md) | [HAL 层](../hal/index.md) | [内核层](../kernel/index.md)
