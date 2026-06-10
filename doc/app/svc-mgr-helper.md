# 服务管理助手（Service Manager Helper）

> **Parent:** [App 层索引](index.md) | **Related:** [WiFi 管理器](../../src/app/wifi/wifi_manager.cpp), [蓝牙管理器](../../src/app/bluetooth/bt_manager.cpp)

## 概述

⚠️ **该模块已移除**。原 `svc_mgr_helper.c/h` 中的 `svc_mgr_handle_switch_toggle()` 工具函数已被内联到各管理器中。WiFi 和蓝牙管理器现在各自独立处理开关切换逻辑。

---

## 当前实现

### WiFi 开关切换

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L288-L296)*

```c
void wifi_mgr_on_switch_toggle(void *ud) {
    (void)ud;
    if (g_wifi_on) {
        wifi_mgr_enable();
    } else {
        wifi_mgr_disable();
    }
}
```

### 蓝牙开关切换

*📄 Source: [bt_manager.cpp](../../src/app/bluetooth/bt_manager.cpp#L235-L242)*

```c
void bt_mgr_on_switch_toggle(void *ud) {
    (void)ud;
    if (g_bt_on) {
        bt_mgr_request_enable();
    } else {
        bt_mgr_request_disable();
    }
}
```

---

## 设计说明

各服务管理器直接根据全局开关标志 `g_wifi_on` / `g_bt_on` 调用自身的 enable/disable 函数：

- **WiFi**：`wifi_mgr_enable()` / `wifi_mgr_disable()` 是同步阻塞调用，直接控制 WiFi 状态机
- **蓝牙**：`bt_mgr_request_enable()` / `bt_mgr_request_disable()` 是异步请求，由 `bt_mgr_update()` 在独立任务循环中处理状态转换

两者均通过 `xerintosh_new_switch_item()` 在 `app_init.c` 中注册为 switch_item 的回调：

*📄 Source: [app_init.c](../../src/app/app_init.c#L163-L164)*

```c
xerintosh_list_item_t* sw1 = xerintosh_new_switch_item(
    "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
```

*📄 Source: [app_init.c](../../src/app/app_init.c#L345-L356)*

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

注意：`app_init_managers()` 中不再在初始化时自动启用蓝牙（`g_bt_on` 不触发 `bt_mgr_enable()`），因为蓝牙初始化已延迟到 `deferred_kernel_init()` 中执行。

---

> **See Also:** [App 层索引](index.md) | [应用初始化](app-init.md)
