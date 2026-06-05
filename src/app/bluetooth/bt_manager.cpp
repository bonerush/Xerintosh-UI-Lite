/**
 * @file   bt_manager.cpp
 * @brief  蓝牙管理器实现（Classic Bluetooth SPP）
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：Classic Bluetooth SPP 简化实现。
 *            BLE 扫描/配对功能已移除，仅保留开关生命周期管理。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include "app/bluetooth/bt_manager.h"

void bt_mgr_init(void) {}
void bt_mgr_enable(void) {}
void bt_mgr_disable(void) {}
bool bt_mgr_is_waiting_input(void) { return false; }
bool bt_mgr_is_enabled(void) { return false; }
void bt_mgr_update(void) {}
void bt_mgr_on_switch_toggle(void *ud) { (void)ud; }

#else

#include <Arduino.h>
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"

extern "C" {
#include "app/wifi/wifi_manager.h"
}

extern bool g_bt_on;  /* 定义在 main.cpp */

static bool g_bt_enabled = false;
static bool g_wifi_was_on = false;  /* BT 启用前 WiFi 是否处于开启状态 */
static bt_mgr_state_t g_state = BT_MGR_IDLE;

void bt_mgr_init(void) {
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;
}

void bt_mgr_enable(void) {
    if (g_bt_enabled) return;

    /* BT/WiFi 互斥：启用 BT 前关闭 WiFi 以释放内存 */
    g_wifi_was_on = wifi_mgr_is_enabled();
    if (g_wifi_was_on) {
        wifi_mgr_disable();
    }

    if (!bt_uart_service_init()) {
        Serial.println("[BT] bt_uart_service_init failed");
        /* 初始化失败时恢复 WiFi */
        if (g_wifi_was_on) {
            wifi_mgr_enable();
        }
        return;
    }
    g_bt_enabled = true;
    g_state = BT_MGR_ENABLED;
}

void bt_mgr_disable(void) {
    bt_uart_service_deinit();
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;

    /* BT/WiFi 互斥：BT 关闭后恢复之前被关闭的 WiFi */
    if (g_wifi_was_on) {
        wifi_mgr_enable();
        g_wifi_was_on = false;
    }
}

bool bt_mgr_is_waiting_input(void) {
    /* Classic BT SPP 不需要串口输入配对码 */
    return false;
}

bool bt_mgr_is_enabled(void) {
    return g_bt_enabled;
}

void bt_mgr_update(void) {
    if (!g_bt_enabled && g_state == BT_MGR_IDLE) return;

    switch (g_state) {
    case BT_MGR_ENABLED:
    case BT_MGR_CONNECTED: {
        /* bt_uart_poll() 已移至 main.cpp loop() 中调用，
         * 确保 g_bt_serial.connected()/read() 与 begin() 在同一任务上下文。
         * 此处仅根据 g_connected 更新状态机。 */
        bool connected = bt_uart_is_connected();
        g_state = connected ? BT_MGR_CONNECTED : BT_MGR_ENABLED;
        break;
    }
    default:
        break;
    }
}

void bt_mgr_on_switch_toggle(void *ud) {
    (void)ud;
    if (g_bt_on) {
        bt_mgr_enable();
    } else {
        bt_mgr_disable();
    }
}

extern "C" void bt_mgr_task_main(void *arg)
{
    (void)arg;
    for (;;) {
        bt_mgr_update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

#endif /* NATIVE_TEST */
