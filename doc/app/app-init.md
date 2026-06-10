# 应用初始化模块（App Init）

> **Parent:** [知识地图](../index.md) | **Related:** [设置管理](settings.md), [项目系统](../ui/item.md)

## 概述

`app_init` 模块负责构建 UI 菜单树和初始化 WiFi/蓝牙管理器。它将原本分散在 `main.cpp` 中的菜单构建逻辑和管理器初始化逻辑提取到独立的 C 模块中，使 `main.cpp` 只保留 Arduino 框架入口和硬件相关的回调。

---

## 职责边界

| 文件 | 职责 |
|------|------|
| `main.cpp` | Arduino `setup()`/`loop()` 入口、M5.Display 硬件调用、设置变更回调 |
| `app_init.c` | 构建菜单树、初始化 WiFi/BT 管理器、按键输入处理 |
| `settings.c` | 从存储加载设置、档位-硬件值转换 |

---

## 菜单构建

*📄 Source: [app_init.c](../../src/app/app_init.c#L147-L232)*

```c
void app_init_ui(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    xerintosh_list_item_t* item1 = xerintosh_new_list_item("设置", list_icon);
    xerintosh_list_item_t* item2 = xerintosh_new_user_item(
        "任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon);
    xerintosh_list_item_t* item3 = xerintosh_new_user_item(
        "串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit, default_icon);
    xerintosh_list_item_t* tu_item = xerintosh_new_user_item(
        "Token Usage", token_usage_init, token_usage_loop, token_usage_exit, default_icon);
    xerintosh_list_item_t* flasher_item = xerintosh_new_user_item(
        "烧录器", flasher_init, flasher_loop, flasher_exit, default_icon);
    xerintosh_list_item_t* item4 = xerintosh_new_user_item(
        "关于", about_init, about_loop, about_exit, user_icon);

    xerintosh_list_item_t* sw1 = xerintosh_new_switch_item(
        "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t* sl1 = xerintosh_new_slider_item(
        "亮度", &g_brightness_level, 1, 1, 10, NULL, on_brightness_change_cb, default_icon);
    xerintosh_list_item_t* sw_anim = xerintosh_new_switch_item(
        "动画效果", &g_anim_enabled, NULL, on_anim_enabled_change_cb, default_icon);
    xerintosh_list_item_t* sl_anim = xerintosh_new_slider_item(
        "动画速度", &g_anim_speed_level, 1, 1, 10, NULL, on_anim_speed_change_cb, default_icon);
    xerintosh_list_item_t* sw_rot = xerintosh_new_switch_item(
        "横屏/竖屏", &g_is_landscape, NULL, on_screen_rotation_change_cb, default_icon);

    /* 波特率子菜单 */
    xerintosh_list_item_t* baud_menu = xerintosh_new_list_item("波特率", list_icon);
    const char *baud_labels[] = {"9600", "19200", "38400", "57600", "115200", "230400"};
    int16_t baud_levels[] = {1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 6; i++) {
        xerintosh_list_item_t* btn = xerintosh_new_button_item(
            baud_labels[i], on_baud_selected_cb, default_icon);
        btn->user_data = (void*)(intptr_t)baud_levels[i];
        xerintosh_push_item_to_list(baud_menu, btn);
    }

    /* 烧录器引脚映射子菜单 */
    xerintosh_list_item_t* flasher_pin_menu = xerintosh_new_list_item("烧录器引脚", list_icon);
    uint8_t pin_nums[] = {0, 26, 36};
    for (int i = 0; i < 3; i++) {
        update_flasher_pin_label(pin_nums[i]);
        if (i == 2) {
            /* G36: 输入引脚，不可更改 */
            xerintosh_list_item_t* pin_item = xerintosh_new_button_item(
                g_pin_label_bufs[i], on_g36_pressed_cb, default_icon);
            pin_item->user_data = (void*)(intptr_t)pin_nums[i];
            xerintosh_push_item_to_list(flasher_pin_menu, pin_item);
        } else {
            /* G0 / G26: 可选 BOOT/DTR 或 TX */
            xerintosh_list_item_t* pin_item = xerintosh_new_list_item(
                g_pin_label_bufs[i], default_icon);
            pin_item->user_data = (void*)(intptr_t)pin_nums[i];
            pin_item->init_function = on_enter_flasher_submenu;
            for (int j = 0; j < FLASHER_ROLE_OPTION_COUNT; j++) {
                xerintosh_list_item_t* role_btn = xerintosh_new_button_item(
                    g_sub_label_bufs[i][j], on_flasher_role_selected_cb, default_icon);
                role_btn->user_data = (void*)(intptr_t)g_role_options[j].role;
                xerintosh_push_item_to_list(pin_item, role_btn);
            }
            xerintosh_push_item_to_list(flasher_pin_menu, pin_item);
        }
    }

    xerintosh_push_item_to_list(root, item1);
    xerintosh_push_item_to_list(root, item2);
    xerintosh_push_item_to_list(root, item3);
    xerintosh_push_item_to_list(root, tu_item);
    xerintosh_push_item_to_list(root, flasher_item);
    xerintosh_push_item_to_list(root, item4);  /* 关于（永远最后） */
    xerintosh_push_item_to_list(item1, sw1);
    xerintosh_push_item_to_list(item1, sl1);
    xerintosh_push_item_to_list(item1, sw_anim);
    xerintosh_push_item_to_list(item1, sl_anim);
    xerintosh_push_item_to_list(item1, sw_rot);
    xerintosh_push_item_to_list(item1, flasher_pin_menu);
    xerintosh_push_item_to_list(item1, baud_menu);
}
```

### 中文伪代码拆解

```
函数 应用初始化_界面() {
    根节点 = 获取根列表()

    设置项 = 新建列表项("设置", 列表图标)
    任务管理器 = 新建用户项("任务管理器", ...)
    串口监视器 = 新建用户项("串口监视器", ...)
    Token Usage = 新建用户项("Token Usage", ...)
    烧录器 = 新建用户项("烧录器", ...)
    关于 = 新建用户项("关于", ...)

    WiFi开关 = 新建开关项("WiFi", wifi状态指针, 回调)
    亮度滑条 = 新建滑条项("亮度", 亮度档位指针, 范围1-10, 变更回调)
    动画开关 = 新建开关项("动画效果", 动画开关指针, 回调)
    速度滑条 = 新建滑条项("动画速度", 速度档位指针, 范围1-10, 回调)
    方向开关 = 新建开关项("横屏/竖屏", 方向开关指针, 回调)
    波特率菜单 = 新建波特率子菜单()
    烧录器引脚菜单 = 新建烧录器引脚子菜单(G0/G26/G36)

    挂载(根节点, 设置项)
    挂载(根节点, 任务管理器)
    挂载(根节点, 串口监视器)
    挂载(根节点, Token Usage)
    挂载(根节点, 烧录器)
    挂载(根节点, 关于)
    挂载(设置项, WiFi开关)
    挂载(设置项, 亮度滑条)
    挂载(设置项, 动画开关)
    挂载(设置项, 速度滑条)
    挂载(设置项, 方向开关)
    挂载(设置项, 烧录器引脚菜单)
    挂载(设置项, 波特率菜单)
}
```

**核心思想**：菜单树是一次性构建的静态结构。所有控件指针（如 `&g_wifi_on`、`&g_brightness_level`）指向外部变量，UI 框架通过指针直接读写状态。

**注意**：蓝牙开关未在设置菜单中构建。蓝牙通过 `deferred_kernel_init()` 延迟初始化，不在 `app_init_ui()` 中创建开关项。

---

## 管理器初始化

*📄 Source: [app_init.c](../../src/app/app_init.c#L345-L360)*

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

---

## 输入处理

*📄 Source: [app_init.c](../../src/app/app_init.c#L372-L495)*

```c
void app_input_process(void)
{
    hal_input_update();

    /* 更新电源键弹窗（检测 A+B 双键关机事件） */
    power_key_popup_update();

    /* 刷新 WiFi 弹窗（跨任务弹窗，每帧 push 以保持显示） */
    wifi_popup_refresh();

    /* 延迟弹窗：按钮回调通过标志位请求 push，在此安全上下文执行 */
    if (g_deferred_popup_pending) {
        g_deferred_popup_pending = false;
        xerintosh_push_pop_up("G36 为输入串口，不可更改", 1500);
    }

    /* 双键按住模式下，隔离所有正常按钮事件，防止 UI 抖动 */
    if (power_key_popup_is_dual_active()) {
        return;
    }

    /* 若处于 user_item 内部，框架输入由 App 自身接管 */
    if (xerintosh_is_in_user_item()) {
        return;
    }

    /* 进/退场动画期间禁止框架输入，避免误触发 */
    if (!g_xerintosh_exit_animation_finished) {
        return;
    }

    /* ── 烧录器子菜单强制解除状态机 ── */
    if (g_flasher_sub_state == FLASHER_SUB_WAITING_FORCE_RELEASE) {
        /* 显示倒计时弹窗，BtnB 取消，BtnA 长按 800ms 确认强制解除 */
        /* ... */
        return;
    }

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

### 按键映射

| 按键 | 短按 | 长按（≥500ms） |
|------|------|----------------|
| **Btn A** | 下一个项 | 确认/进入 |
| **Btn B** | 上一个项 | 退出/取消 |

### 输入处理流程

```
每帧 app_input_process():
1. hal_input_update()              // 刷新 M5.update() 边沿标志
2. power_key_popup_update()        // A+B 双键关机检测
3. wifi_popup_refresh()            // WiFi 跨任务弹窗保持
4. g_deferred_popup_pending 处理    // G36 不可更改提示
5. power_key_popup_is_dual_active() // 双键模式下隔离正常输入
6. xerintosh_is_in_user_item()      // user_item 内部由 App 接管
7. g_xerintosh_exit_animation_finished // 动画期间禁止输入
8. 烧录器强制解除状态机             // 角色冲突倒计时
9. 正常导航事件处理                 // BtnA/B → 下一项/上一项/确认/退出
```

---

## 外部依赖

`app_init.c` 依赖以下外部变量（由 `main.cpp` 或 `native_main.cpp` 定义）：

| 变量 | 提供者 | 说明 |
|------|--------|------|
| `g_wifi_on` | `main.cpp` / `native_main.cpp` | WiFi 开关状态 |
| `g_bt_on` | `main.cpp` / `native_main.cpp` | 蓝牙开关状态 |
| `g_brightness_level` | `settings.c` | 亮度档位 |
| `g_anim_speed_level` | `settings.c` | 动画速度档位 |
| `g_anim_enabled` | `settings.c` / `ui_context.h` | 动画开关 |
| `g_is_landscape` | `settings.c` | 横屏/竖屏开关 |
| `g_serial_baud_rate` | `settings.c` | 串口波特率档位 |

---

> **See Also:** [设置管理](settings.md) | [项目系统](../ui/item.md) | [输入系统](../hal/input.md)
