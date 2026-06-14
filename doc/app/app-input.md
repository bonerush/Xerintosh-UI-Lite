# App 输入处理模块（App Input）

> **Parent:** [App 层索引](index.md) | **Related:** [App 初始化](app-init.md), [输入系统](../hal/input.md)

## 概述

`app_input` 模块负责每帧读取硬件按键事件并将其映射到 UI 选择器导航，同时调度各模块的独立状态机（电源键弹窗、WiFi 弹窗、烧录器强制解除等）。phase 2.4 重构后，该职责从 `app_init.c` 中独立出来。

---

## 核心 API

### app_input_process()

*📄 Source: [app_input.c](../../src/app/app_input.c#L33-L90)*

每帧由 `main.cpp` 调用（或经 `app_input_process()` 入口转发），按以下顺序处理：

1. `hal_input_update()` —— 刷新按键边沿标志。
2. `power_key_popup_update()` —— A+B 双键关机检测。
3. `wifi_popup_refresh()` —— WiFi 跨任务弹窗保持显示。
4. `flasher_menu_process_input()` —— 烧录器引脚延迟弹窗与强制解除状态机。
5. 双键隔离、user_item 接管、退场动画守卫、强制解除激活守卫。
6. 正常导航：BtnA/B 短按/长按。

---

## 状态机隔离

各状态机通过独立的 "激活" 查询接口与输入路由交互：

| 状态机 | 激活查询 | 消费输入方式 |
|--------|----------|--------------|
| 电源键弹窗 | `power_key_popup_is_dual_active()` | 双键模式下直接 return |
| 烧录器强制解除 | `flasher_menu_is_active()` | 激活时跳过框架导航 |
| user_item | `xerintosh_is_in_user_item()` | 内部由 App 自行处理 |
| 退场动画 | `g_xerintosh_exit_animation_finished` | 动画期间禁止输入 |

这种设计避免了在 `app_input_process()` 中内联复杂状态机，使输入路由保持简洁。

---

## 按键映射

| 按键 | 短按 | 长按（≥500ms） |
|------|------|----------------|
| **Btn A** | 选择器下移（下一项） | 确认/进入当前项 |
| **Btn B** | 选择器上移（上一项） | 退出/取消当前项 |

---

> **See Also:** [App 初始化](app-init.md) | [输入系统](../hal/input.md) | [电源键弹窗](../app/power-key-popup.md)
