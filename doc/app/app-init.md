# 应用初始化模块（App Init）

> **Parent:** [知识地图](../index.md) | **Related:** [设置管理](settings.md), [项目系统](../ui/item.md), [App 菜单构建](app-menu.md), [App 输入处理](app-input.md)

## 概述

`app_init` 模块是 App 层的入口封装，负责把菜单构建、输入处理、管理器初始化等职责分派到各自的子模块。phase 2.4 重构后，`app_init.c` 不再直接实现菜单树构建和输入路由，而是作为薄封装调用 `app_menu.c` 和 `app_input.c`。

---

## 职责边界

| 文件 | 职责 |
|------|------|
| `main.cpp` | Arduino `setup()`/`loop()` 入口、M5.Display 硬件调用、设置变更回调 |
| `app_init.c` | 入口封装：调用 `app_menu_build()`、`app_init_managers()`、`app_input_process()` |
| `app_menu.c` | 构建 Xerintosh UI 菜单树 |
| `app_input.c` | 每帧按键输入路由与状态机调度 |
| `app_state.c` | 集中定义 `g_wifi_on`、`g_bt_on` 等跨模块全局状态 |
| `app/flasher/flasher_menu.c` | 烧录器引脚配置子菜单与强制解除状态机 |
| `ui_service.c` | user_item 公共生命周期辅助（输入重置、标准退出检测） |
| `settings.c` | 从存储加载设置、档位-硬件值转换 |

---

## 入口函数

### app_init_ui()

*📄 Source: [app_init.c](../../src/app/app_init.c#L25-L28)*

```c
void app_init_ui(void)
{
    app_menu_build();
}
```

薄封装，实际菜单构建逻辑在 `app_menu.c` 中。

### app_init_managers()

*📄 Source: [app_init.c](../../src/app/app_init.c#L30-L46)*

```c
void app_init_managers(void)
{
    bt_mgr_init();
    wifi_mgr_init();
    power_key_popup_init();

    /* BT 初始化已移至 deferred_kernel_init()（内核任务 spawn 之后），
       避免在 setup() 中过早消耗内存导致 FreeRTOS 任务创建失败。 */
    if (g_wifi_on) wifi_mgr_enable();
}
```

### app_input_process()

*📄 Source: [app_input.c](../../src/app/app_input.c#L33-L90)*

```c
void app_input_process(void)
{
    hal_input_update();

    /* 更新电源键弹窗（检测 A+B 双键关机事件） */
    power_key_popup_update();

    /* 刷新 WiFi 弹窗（跨任务弹窗，每帧 push 以保持显示） */
    wifi_popup_refresh();

    /* 烧录器引脚菜单：延迟弹窗 + 强制解除状态机 */
    flasher_menu_process_input();

    /* 双键按住模式下，隔离所有正常按钮事件，防止 UI 抖动 */
    if (power_key_popup_is_dual_active()) return;

    /* 若处于 user_item 内部，框架输入由 App 自身接管 */
    if (xerintosh_is_in_user_item()) return;

    /* 进/退场动画期间禁止框架输入，避免误触发 */
    if (!g_xerintosh_exit_animation_finished) return;

    /* 烧录器强制解除状态机激活时，跳过框架导航 */
    if (flasher_menu_is_active()) return;

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
        xerintosh_selector_go_prev_item();
    else if (event_b == HAL_EVENT_LONG_PRESS)
        xerintosh_selector_exit_current_item();

    if (event_a == HAL_EVENT_SHORT_PRESS)
        xerintosh_selector_go_next_item();
    else if (event_a == HAL_EVENT_LONG_PRESS)
        xerintosh_selector_jump_to_selected_item();
}
```

### 输入处理流程

```
每帧 app_input_process():
1. hal_input_update()              // 刷新 M5.update() 边沿标志
2. power_key_popup_update()        // A+B 双键关机检测
3. wifi_popup_refresh()            // WiFi 跨任务弹窗保持
4. flasher_menu_process_input()    // G36 延迟弹窗 + 强制解除状态机
5. power_key_popup_is_dual_active() // 双键模式下隔离正常输入
6. xerintosh_is_in_user_item()      // user_item 内部由 App 接管
7. g_xerintosh_exit_animation_finished // 动画期间禁止输入
8. flasher_menu_is_active()         // 强制解除状态机期间禁止框架导航
9. 正常导航事件处理                 // BtnA/B → 下一项/上一项/确认/退出
```

### 按键映射

| 按键 | 短按 | 长按（≥500ms） |
|------|------|----------------|
| **Btn A** | 下一个项 | 确认/进入 |
| **Btn B** | 上一个项 | 退出/取消 |

---

## 外部依赖

`app_init.c` 依赖以下外部变量与回调：

| 变量/回调 | 提供者 | 说明 |
|------|--------|------|
| `g_wifi_on` | `app_state.c` | WiFi 开关状态 |
| `g_bt_on` | `app_state.c` | 蓝牙开关状态 |
| `g_brightness_level` | `settings.c` | 亮度档位 |
| `g_anim_speed_level` | `settings.c` | 动画速度档位 |
| `g_anim_enabled` | `settings.c` / `ui_context.h` | 动画开关 |
| `g_is_landscape` | `settings.c` | 横屏/竖屏开关 |
| `g_serial_baud_rate` | `settings.c` | 串口波特率档位 |
| `on_*_change_cb` | `main.cpp` / `native_main.cpp` | 设置变更回调 |

---

## 与各子模块的关系

```
main.cpp
    │
    ▼
app_init.c  ──► app_menu.c       构建菜单树
             ──► app_input.c      输入路由
             ──► app_state.c      全局状态
             ──► ui_service.c     user_item 生命周期辅助
             ──► flasher_menu.c   烧录器引脚子菜单
             ──► settings.c       设置加载与转换
             ──► wifi/bt mgr      管理器初始化
```

---

> **See Also:** [App 菜单构建](app-menu.md) | [App 输入处理](app-input.md) | [设置管理](settings.md) | [项目系统](../ui/item.md) | [输入系统](../hal/input.md)
