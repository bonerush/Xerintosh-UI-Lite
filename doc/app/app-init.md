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

*📄 Source: [app_init.c](../../src/app/app_init.c#L50-L80)*

```c
void app_init_ui(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    xerintosh_list_item_t* item1 = xerintosh_new_list_item("设置", list_icon);
    xerintosh_list_item_t* item2 = xerintosh_new_list_item("关于", user_icon);

    xerintosh_list_item_t* sw1 = xerintosh_new_switch_item(
        "WiFi", &wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
    xerintosh_list_item_t* sw2 = xerintosh_new_switch_item(
        "蓝牙", &bt_on, NULL, bt_mgr_on_switch_toggle, default_icon);
    /* ... 更多控件 ... */

    xerintosh_push_item_to_list(root, item1);
    xerintosh_push_item_to_list(root, item2);
    xerintosh_push_item_to_list(item1, sw1);
    /* ... */
}
```

### 中文伪代码拆解

```
函数 应用初始化_界面() {
    根节点 = 获取根列表()

    设置项 = 新建列表项("设置", 列表图标)
    关于项 = 新建列表项("关于", 用户图标)

    WiFi开关 = 新建开关项("WiFi", wifi状态指针, 回调)
    蓝牙开关 = 新建开关项("蓝牙", bt状态指针, 回调)
    亮度滑条 = 新建滑条项("亮度", 亮度档位指针, 范围1-10, 变更回调)
    动画开关 = 新建开关项("动画效果", 动画开关指针, 回调)
    速度滑条 = 新建滑条项("动画速度", 速度档位指针, 范围1-10, 回调)
    方向滑条 = 新建滑条项("屏幕方向", 方向档位指针, 范围1-2, 回调)

    挂载(根节点, 设置项)
    挂载(根节点, 关于项)
    挂载(设置项, WiFi开关)
    /* ... */
}
```

**核心思想**：菜单树是一次性构建的静态结构。所有控件指针（如 `&wifi_on`、`&g_brightness_level`）指向外部变量，UI 框架通过指针直接读写状态。

---

## 管理器初始化

*📄 Source: [app_init.c](../../src/app/app_init.c#L82-L94)*

```c
void app_init_managers(void)
{
    wifi_mgr_init();
    bt_mgr_init();

    if (wifi_on) wifi_mgr_enable();
    if (bt_on)   bt_mgr_enable();
}
```

---

## 输入处理

*📄 Source: [app_init.c](../../src/app/app_init.c#L96-L136)*

```c
void app_input_process(void)
{
    hal_input_update();

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
        xerintosh_selector_go_prev_item();
    else if (event_b == HAL_EVENT_LONG_PRESS)
        /* 处理长按B：串口取消 + 退出当前项 */

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

---

## 外部依赖

`app_init.c` 依赖以下外部变量（由 `main.cpp` 或 `native_main.cpp` 定义）：

| 变量 | 提供者 | 说明 |
|------|--------|------|
| `wifi_on` | `main.cpp` / `native_main.cpp` | WiFi 开关状态 |
| `bt_on` | `main.cpp` / `native_main.cpp` | 蓝牙开关状态 |
| `g_brightness_level` | `settings.c` | 亮度档位 |
| `g_anim_speed_level` | `settings.c` | 动画速度档位 |
| `g_anim_enabled` | `ui_core.c` | 动画开关 |
| `g_screen_rotation_level` | `settings.c` | 屏幕方向档位 |

---

> **See Also:** [设置管理](settings.md) | [项目系统](../ui/item.md) | [输入系统](../hal/input.md)
