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
void bt_mgr_update(void) {}
void bt_mgr_on_switch_toggle(void *ud) { (void)ud; }

#else

#include <Arduino.h>
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"

extern "C" {
#include "app/svc_mgr_helper.h"
#include "app/ui_service.h"
#include "kernel/kern_task.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
}

extern bool g_bt_on;  /* 定义在 main.cpp */

static bool g_bt_enabled = false;
static bt_mgr_state_t g_state = BT_MGR_IDLE;

static xerintosh_list_item_t *g_settings_list = NULL;

void bt_mgr_init(void) {
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;

    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root && root->child_num > 0) {
        g_settings_list = root->child_list_item[0];  /* "设置" */
    }
}

void bt_mgr_enable(void) {
    if (g_bt_enabled) return;

    Serial.println("[BT] bt_mgr_enable called");
    g_bt_enabled = true;

    /* 直接在 setup() 上下文中初始化 BluetoothSerial，避免在 bt-mgr
       内核任务中触发 Bluedroid 的 vQueueDelete(NULL) bug */
    if (bt_uart_service_init()) {
        g_state = BT_MGR_ENABLED;
        Serial.println("[BT] bt_mgr_enable done");
    } else {
        g_bt_enabled = false;
        g_state = BT_MGR_IDLE;
        Serial.println("[BT] bt_uart_service_init failed");
        ui_svc_notify_error("蓝牙启动失败");
    }
}

void bt_mgr_disable(void) {
    bt_uart_service_deinit();
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;
}

bool bt_mgr_is_waiting_input(void) {
    /* Classic BT SPP 不需要串口输入配对码 */
    return false;
}

void bt_mgr_update(void) {
    if (!g_bt_enabled && g_state == BT_MGR_IDLE) return;

    static bt_mgr_state_t last_state = BT_MGR_IDLE;
    if (g_state != last_state) {
        Serial.printf("[BT] State: %d -> %d\n", last_state, g_state);
        last_state = g_state;
    }

    switch (g_state) {
    case BT_MGR_ENABLED:
    case BT_MGR_CONNECTED: {
        bt_uart_poll();
        bool connected = bt_uart_is_connected();
        g_state = connected ? BT_MGR_CONNECTED : BT_MGR_ENABLED;
        break;
    }
    default:
        break;
    }
}

void bt_mgr_on_switch_toggle(void *ud) {
    svc_mgr_handle_switch_toggle(&g_bt_on, bt_mgr_enable, bt_mgr_disable, ud);
}

extern "C" void bt_mgr_task_main(void *arg)
{
    (void)arg;
    kern_poll_loop(bt_mgr_update, 50);
}

#endif /* NATIVE_TEST */
