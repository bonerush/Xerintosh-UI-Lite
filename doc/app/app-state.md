# App 层全局状态（App State）

> **Parent:** [App 层索引](index.md) | **Related:** [应用初始化](app-init.md), [设置管理](settings.md), [WiFi 管理器](wifi.md), [蓝牙管理器](bluetooth.md)

## 概述

`app_state` 模块集中声明 App 层跨模块共享的全局变量与设置变更回调。通过把 `g_wifi_on`、`g_bt_on` 等状态从 `main.cpp` 和各管理器中收敛到一处，降低模块间的隐式耦合。

## 关键概念

### 无线开关状态

*📄 Source: [app_state.c](../../src/app/app_state.c#L11-L13)*

```c
/* WiFi 默认开启（BT 默认关闭，内存充足） */
bool g_wifi_on = true;
bool g_bt_on   = false;
```

- `g_wifi_on`：WiFi 开关状态，默认 `true`（开机自启）
- `g_bt_on`：蓝牙开关状态，默认 `false`（按需启用，避免与 WiFi 争用内存）

这两个变量被 `app_menu.c` 中的 `switch_item` 直接绑定，用户切换开关时会立即修改全局状态，再由对应管理器在后续帧中异步执行启用/禁用。

### 设置变更回调

*📄 Source: [app_state.h](../../src/app/app_state.h#L27-L34)*

```c
extern void on_brightness_change_cb(void *ud);
extern void on_anim_speed_change_cb(void *ud);
extern void on_anim_enabled_change_cb(void *ud);
extern void on_screen_rotation_change_cb(void *ud);
extern void on_serial_baud_change_cb(void *ud);
extern void on_spring_mode_change_cb(void *ud);
extern void on_spring_stiffness_change_cb(void *ud);
extern void on_spring_damping_change_cb(void *ud);
```

这些回调在 `main.cpp` / `native_main.cpp` 中实现，原因：
- 回调需要调用 `M5.Display`、Arduino `Serial` 等 C++ API
- `main.cpp` 是唯一的 C++ 入口文件，天然持有这些硬件对象

当 `slider_item` 或 `switch_item` 的值变化时，对应的 `on_*_change_cb(NULL)` 被触发，将档位值应用到硬件。

### 使用方

*📄 Source: [app_init.c](../../src/app/app_init.c#L26-L44)*

```c
void app_init_managers(void)
{
    bt_mgr_init();
    wifi_mgr_init();
    power_key_popup_init();

    xerintosh_set_dual_key_callback(power_key_popup_is_dual_active);

    if (g_wifi_on) wifi_mgr_enable();
}
```

`app_init_managers()` 在启动时读取 `g_wifi_on`，决定是否立即启用 WiFi。蓝牙则延迟到内核任务创建后再初始化，避免过早占用内存导致 FreeRTOS 任务创建失败。

---

> **See Also:** [应用初始化](app-init.md) | [设置管理](settings.md) | [WiFi 管理器](wifi.md) | [蓝牙管理器](bluetooth.md)
