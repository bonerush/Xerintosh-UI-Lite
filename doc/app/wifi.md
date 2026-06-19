# WiFi 管理器（WiFi Manager）

> **Parent:** [App 层索引](index.md) | **Related:** [串口输入](serial-input.md), [存储](storage.md), [设置管理](settings.md), [蓝牙管理器](bluetooth.md)

## 概述

`wifi_manager` 是 WiFi 状态机与 UI 菜单的封装。它管理 WiFi 启用/禁用、异步扫描、串口密码输入、已保存网络与可用网络的动态菜单重建，并与蓝牙管理器通过互斥标志协调射频/内存资源。

## 关键概念

### 状态机

*📄 Source: [wifi_manager.h](../../src/app/wifi/wifi_manager.h#L28-L36)*

```c
typedef enum {
    WIFI_MGR_IDLE,           /* 空闲/关闭 */
    WIFI_MGR_WARMUP,         /* 预热中 */
    WIFI_MGR_SCANNING,       /* 扫描中 */
    WIFI_MGR_SCAN_DONE,      /* 扫描完成 */
    WIFI_MGR_CONNECTING,     /* 连接中 */
    WIFI_MGR_CONNECTED,      /* 已连接 */
    WIFI_MGR_CONNECT_FAILED  /* 连接失败 */
} wifi_mgr_state_t;
```

状态流转：`IDLE → WARMUP → SCANNING → SCAN_DONE → CONNECTING → CONNECTED/CONNECT_FAILED`。

### 初始化

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L179-L203)*

```c
void wifi_mgr_init(void)
{
    g_enable_requested  = false;
    g_disable_requested = false;
    g_bt_was_on         = false;

    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root) {
        for (int i = 0; i < root->child_num; i++) {
            if (root->child_list_item[i] &&
                root->child_list_item[i]->content &&
                strcmp(root->child_list_item[i]->content, "设置") == 0) {
                g_settings_list = root->child_list_item[i];
                break;
            }
        }
    }
}
```

初始化时不立即连接，仅按名称查找“设置”菜单项句柄，供后续动态挂载网络子菜单。

### 启用与 BT/WiFi 互斥

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L210-L250)*

```c
void wifi_mgr_enable(void)
{
    if (bt_mgr_is_enabled()) {
        wifi_popup_request("请先关闭蓝牙", 2000);
        g_wifi_enabled = false;
        g_state = WIFI_MGR_IDLE;
        return;
    }

    if (ESP.getFreeHeap() < 45000) {
        wifi_popup_request("内存不足", 2000);
        ...
        return;
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);

    g_warmup_start_time = millis();
    g_state = WIFI_MGR_WARMUP;
    wifi_menu_rebuild_list(0);
}
```

启用前检查：
1. 蓝牙已开启时拒绝直接启用（弹窗提示先关闭蓝牙）
2. 剩余堆内存不足 45KB 时拒绝启用
3. 进入 STA 模式、关闭省电、注册扫描完成回调

### 异步扫描

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L458-L480)*

```c
void wifi_menu_on_scan_pressed(void *ud)
{
    (void)ud;
    if (g_connecting) {
        WiFi.disconnect();
        restore_wifi_logs();
        g_connecting = false;
    }

    g_scan_done = false;
    g_scan_ap_count = 0;
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = true;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        wifi_popup_request("扫描失败", 2000);
        g_state = WIFI_MGR_SCAN_DONE;
    } else {
        g_state = WIFI_MGR_SCANNING;
        g_wifi_scan_start_time = millis();
        wifi_popup_request("扫描中...", WIFI_SCAN_TIMEOUT_MS);
    }
}
```

扫描使用 ESP-IDF 异步 API `esp_wifi_scan_start(..., false)`，避免在主任务中阻塞。扫描完成后通过事件回调设置 `g_scan_done` 标志，再由 `wifi_mgr_update()` 读取结果。

### 每帧状态机更新

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L551-L692)*

```c
void wifi_mgr_update(void)
{
    if (!g_wifi_enabled && g_state == WIFI_MGR_IDLE) return;

    switch (g_state) {
    case WIFI_MGR_WARMUP:
        if (millis() - g_warmup_start_time >= WIFI_WARMUP_DELAY_MS) {
            /* 启动首次扫描 */
            ...
        }
        break;

    case WIFI_MGR_SCANNING:
        if (g_scan_done) {
            int16_t result = WiFi.scanComplete();
            wifi_menu_rebuild_list(result >= 0 ? result : 0);
            g_state = WIFI_MGR_SCAN_DONE;
        }
        break;

    case WIFI_MGR_CONNECTING:
        /* 轮询串口密码、检查连接超时与 WL_CONNECTED */
        ...
        break;

    case WIFI_MGR_SCAN_DONE:
        if (!g_auto_connect_done) {
            g_auto_connect_done = true;
            try_auto_connect();
        }
        break;

    default:
        break;
    }
}
```

`wifi_mgr_update()` 在主循环中每帧调用，处理：
- 预热倒计时
- 扫描完成/超时
- 串口密码轮询
- 连接状态检测
- 扫描完成后自动连接最佳已保存网络

### 串口密码输入

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L386-L398)*

```c
void wifi_menu_on_network_button_pressed(void *ud)
{
    (void)ud;
    const char *content = g_xerintosh_selector.selected_item->content;
    if (!content) return;

    wifi_popup_request("请在串口输入密码", 8000);
    serial_request_wifi_password(content);
    g_is_auto_connect = false;
    g_state = WIFI_MGR_CONNECTING;
}
```

用户选择可用网络后，调用 `serial_request_wifi_password()` 请求通过串口输入密码，状态机进入 `CONNECTING`。

### 异步请求接口

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp#L330-L364)*

```c
void wifi_mgr_request_enable(void)
{
    g_disable_requested = false;
    g_enable_requested  = true;
}

void wifi_mgr_request_disable(void)
{
    g_enable_requested  = false;
    g_disable_requested = true;
}

void wifi_mgr_process_requests(void)
{
    if (g_disable_requested) {
        g_disable_requested = false;
        wifi_mgr_disable();
        return;
    }

    if (g_enable_requested) {
        if (bt_mgr_is_enabled()) {
            bt_mgr_request_disable();
            return;
        }
        g_enable_requested = false;
        wifi_mgr_enable();
    }
}
```

UI 任务通过 `request_*()` 发起启用/禁用请求，实际驱动操作统一在 `wifi_mgr_process_requests()` 中执行，确保 `WiFi.*` API 调用都在 Arduino `loop()` 任务上下文。

## 动态网络菜单

网络列表由 `wifi_menu.c` 根据扫描结果和已保存网络动态构建，挂载到“设置”菜单下。详见 `src/app/wifi/wifi_menu.c`。

---

> **See Also:** [串口输入](serial-input.md) | [存储](storage.md) | [设置管理](settings.md) | [蓝牙管理器](bluetooth.md)
