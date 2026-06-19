# 蓝牙管理器（Bluetooth Manager）

> **Parent:** [App 层索引](index.md) | **Related:** [串口监视器](serial-monitor.md), [服务管理助手](svc-mgr-helper.md), [WiFi 管理器](wifi.md), [串口输入](serial-input.md)

## 概述

`bluetooth` 模块提供经典蓝牙 SPP（Serial Port Profile）生命周期管理，包含状态机、启用/禁用、异步请求接口与 UART 数据服务。由于 ESP32 的 BT 与 WiFi 共享射频和内存池，本模块采用与 WiFi 互斥的启用策略，并通过异步请求确保 BluetoothSerial API 始终在 Arduino 主任务中调用。

## 关键概念

### 状态机

*📄 Source: [bt_manager.h](../../src/app/bluetooth/bt_manager.h#L37-L42)*

```c
typedef enum {
    BT_MGR_IDLE,       /* 空闲/关闭 */
    BT_MGR_WARMUP,     /* 预热中 */
    BT_MGR_ENABLED,    /* 已启用，等待连接 */
    BT_MGR_CONNECTED,  /* 有客户端连接 */
} bt_mgr_state_t;
```

### 启用流程与 BT/WiFi 互斥

*📄 Source: [bt_manager.cpp](../../src/app/bluetooth/bt_manager.cpp#L94-L144)*

```c
void bt_mgr_enable(void)
{
    if (g_bt_enabled) return;

    const uint32_t BT_MIN_HEAP = 70000;
    if (ESP.getFreeHeap() < BT_MIN_HEAP) {
        Serial.printf("[BT] FATAL: heap too low for BT (%u < %u).\n",
                      ESP.getFreeHeap(), BT_MIN_HEAP);
        Serial.flush();
        return;
    }

    g_wifi_was_on = wifi_mgr_is_enabled();
    if (g_wifi_was_on) {
        wifi_mgr_disable();
        delay(500);  /* 等待 WiFi 栈内存完全释放 */
    }

    if (!bt_uart_service_init()) {
        Serial.println("[BT] bt_uart_service_init failed");
        if (g_wifi_was_on) wifi_mgr_enable();
        return;
    }
    g_bt_enabled = true;
    g_state = BT_MGR_ENABLED;
}
```

启用 Bluetooth 前：
1. 检查剩余堆内存是否 >= 70KB
2. 记录当前 WiFi 状态到 `g_wifi_was_on`
3. 若 WiFi 开启，先关闭 WiFi 并延时 500ms 释放内存
4. 调用 `bt_uart_service_init()` 初始化 Bluedroid SPP

### 异步请求接口

*📄 Source: [bt_manager.cpp](../../src/app/bluetooth/bt_manager.cpp#L165-L213)*

```c
void bt_mgr_request_enable(void)
{
    g_enable_requested = true;
    g_disable_requested = false;
}

void bt_mgr_request_disable(void)
{
    g_disable_requested = true;
    g_enable_requested = false;
}

void bt_mgr_process_requests(void)
{
    if (g_disable_requested) {
        g_disable_requested = false;
        if (g_bt_enabled) bt_mgr_disable();
    }
    if (g_enable_requested) {
        g_enable_requested = false;
        if (!g_bt_enabled) bt_mgr_enable();
    }
}
```

`request_*()` 可从任意任务安全调用；`process_requests()` 必须在 Arduino `loop()` 中每帧调用，以在正确的任务上下文执行 BluetoothSerial 操作。

### UART 服务

*📄 Source: [bt_uart_service.h](../../src/app/bluetooth/bt_uart_service.h#L42-L121)*

```c
bool bt_uart_service_init(void);
void bt_uart_service_deinit(void);

uint16_t bt_uart_send(const uint8_t *data, uint16_t len);
uint16_t bt_uart_send_string(const char *str);

void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback);
void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback);

bool bt_uart_is_connected(void);
void bt_uart_poll(void);
void bt_uart_drain_rx_queue(void);
```

`bt_uart_service` 负责：
- 初始化/反初始化 `BluetoothSerial`
- 提供发送与连接状态查询 API
- 在主任务中轮询 RX 数据并通过 FreeRTOS 队列传递给 UI 任务
- `bt_uart_drain_rx_queue()` 在 UI 任务中消费队列并调用应用注册的 RX 回调

### 线程安全

*📄 Source: [bt_manager.h](../../src/app/bluetooth/bt_manager.h#L7-L13)*

BluetoothSerial 的 `begin()` / `connected()` / `read()` 必须在 Arduino 主任务中调用。UI 任务通过 `bt_mgr_request_enable/disable()` 发起请求，避免跨任务调用导致 Bluedroid 内部死锁 → TWDT 看门狗复位。

### 菜单开关回调

*📄 Source: [bt_manager.cpp](../../src/app/bluetooth/bt_manager.cpp#L242-L249)*

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

设置菜单中的“蓝牙”开关直接绑定 `g_bt_on`，切换时通过 `bt_mgr_on_switch_toggle()` 发起异步启用/禁用请求。

---

> **See Also:** [串口监视器](serial-monitor.md) | [服务管理助手](svc-mgr-helper.md) | [WiFi 管理器](wifi.md) | [串口输入](serial-input.md)
