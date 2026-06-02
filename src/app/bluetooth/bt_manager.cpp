/**
 * @file   bt_manager.cpp
 * @brief  蓝牙管理器实现
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

extern "C" {
#include "app/svc_mgr_helper.h"
#include "kernel/kern_task.h"
}

extern bool g_bt_on;  /* 定义在 main.cpp */

static bool g_bt_enabled = false;

void bt_mgr_init(void) {
    g_bt_enabled = false;
}

void bt_mgr_enable(void) {
    g_bt_enabled = true;
}

void bt_mgr_disable(void) {
    g_bt_enabled = false;
}

bool bt_mgr_is_waiting_input(void) {
    return false;
}

void bt_mgr_update(void) {
    (void)g_bt_enabled;
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
