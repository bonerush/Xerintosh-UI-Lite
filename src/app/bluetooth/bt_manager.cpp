/**
 * @file   bt_manager.cpp
 * @brief  蓝牙管理器实现（Classic Bluetooth SPP）
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：Classic Bluetooth SPP 简化实现。
 *            BLE 扫描/配对功能已移除，仅保留开关生命周期管理。
 *
 *          线程安全架构（v2）：
 *          - BluetoothSerial API 调用（begin/connected/read）必须全部在
 *            Arduino main loop() 任务上下文中执行。
 *          - UI 任务通过 bt_mgr_request_enable/disable() 发起异步请求。
 *          - bt_mgr_process_requests() 在 loop() 中执行实际操作。
 *          - 这避免了跨任务 Bluedroid 死锁 → TWDT 看门狗复位。
 *
 *          ═══ BT/WiFi 共存模式（标准模板，后续开发者请遵循） ═══
 *          由于 ESP32 的 BT (Bluedroid) 与 WiFi 共享同一射频和内存池，
 *          同时开启两者会导致堆内存不足与射频冲突。本模块采用以下模式：
 *
 *          启用 BT 流程：
 *              1. 检查当前堆内存 >= 70KB（BT 初始化约需 92KB）
 *              2. 保存 WiFi 状态 (g_wifi_was_on)
 *              3. 若 WiFi 开启 → wifi_mgr_disable() → delay(500ms)
 *                 （等待 WiFi 栈完全释放 ~34KB 内存，esp_wifi_deinit 为异步操作）
 *              4. bt_uart_service_init() 初始化 Bluedroid SPP
 *              5. 成功后标记 g_bt_enabled = true
 *
 *          禁用 BT 流程：
 *              1. bt_uart_service_deinit() 关闭 BT 栈
 *              2. 若 g_wifi_was_on → wifi_mgr_enable() 恢复 WiFi
 *              3. 标记 g_bt_enabled = false
 *
 *          任何需要 BT 的 App（串口监视器 BLE 模式、烧录器透传模式等）
 *          都应通过 bt_mgr_request_enable() / bt_mgr_disable() 操作，而
 *          不是手动调用 bt_uart_service_init/deinit。这些函数已统一处理
 *          WiFi ↔ BT 互斥逻辑与线程安全。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include "app/bluetooth/bt_manager.h"

static bool g_bt_enabled_test = false;

void bt_mgr_init(void) { g_bt_enabled_test = false; }
void bt_mgr_enable(void) { g_bt_enabled_test = true; }
void bt_mgr_disable(void) { g_bt_enabled_test = false; }
void bt_mgr_request_enable(void) { g_bt_enabled_test = true; }
void bt_mgr_request_disable(void) { g_bt_enabled_test = false; }
void bt_mgr_process_requests(void) {}
bool bt_mgr_is_waiting_input(void) { return false; }
bool bt_mgr_is_enabled(void) { return g_bt_enabled_test; }
void bt_mgr_update(void) {}
void bt_mgr_on_switch_toggle(void *ud) { (void)ud; }

void bt_mgr_test_set_enabled(bool enabled)
{
    g_bt_enabled_test = enabled;
}

#else

/* bt_manager 调试开关 */
#define BT_MGR_DBG_ENABLED 0

#include <Arduino.h>
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"

extern "C" {
#include "app/wifi/wifi_manager.h"
#include "kernel/kern_task.h"
}

extern bool g_bt_on;  /* 定义在 app/app_state.c */

static bool g_bt_enabled = false;
static bool g_wifi_was_on = false;  /* BT 启用前 WiFi 是否处于开启状态 */
static bt_mgr_state_t g_state = BT_MGR_IDLE;

/* ═══ 异步请求标志 ═══ */
static volatile bool g_enable_requested  = false;
static volatile bool g_disable_requested = false;

void bt_mgr_init(void) {
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;
    g_enable_requested  = false;
    g_disable_requested = false;
}

void bt_mgr_enable(void) {
    if (g_bt_enabled) return;

#if BT_MGR_DBG_ENABLED
    Serial.printf("[BT-DBG] bt_mgr_enable() entry free_heap=%u ms=%lu\n",
                  ESP.getFreeHeap(), (unsigned long)millis()); Serial.flush();
#endif

    /* ═══ 堆内存安全守卫 ═══
     * Bluedroid Classic BT SPP 初始化 + 连接建链约需 ~70KB。
     * （BLE 内存已在 bt_uart_service_init 中通过
     *  esp_bt_controller_mem_release 释放，节省 ~25KB） */
    const uint32_t BT_MIN_HEAP = 70000;
    if (ESP.getFreeHeap() < BT_MIN_HEAP) {
        Serial.printf("[BT] FATAL: heap too low for BT (%u < %u).\n",
                      ESP.getFreeHeap(), BT_MIN_HEAP);
        Serial.flush();
        return;
    }

    /*
     * ═══ BT/WiFi 互斥模式 ═══
     * 启用 BT 前必须关闭 WiFi 以释放 ~34KB 堆内存。
     * g_wifi_was_on 记录原始状态，供 bt_mgr_disable() 恢复用。
     *
     * 关键：esp_wifi_deinit() 是异步操作，WiFi 内存不会立即释放。
     * 需要 delay(500ms) 等待 FreeRTOS WiFi 任务完成清理，否则
     * Bluedroid 初始化时堆内存不足，导致 SPP 连接失败。
     * （参考串口监视器 sm_app_init 中 hal_delay_ms(500) 模式）
     */
    g_wifi_was_on = wifi_mgr_is_enabled();
    if (g_wifi_was_on) {
        wifi_mgr_disable();
        delay(500);  /* 等待 WiFi 栈内存完全释放 */
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
#if BT_MGR_DBG_ENABLED
    Serial.printf("[BT-DBG] bt_mgr_enable() done free_heap=%u ms=%lu\n",
                  ESP.getFreeHeap(), (unsigned long)millis()); Serial.flush();
#endif
}

void bt_mgr_disable(void) {
#if BT_MGR_DBG_ENABLED
    Serial.printf("[BT-DBG] bt_mgr_disable() free_heap=%u ms=%lu\n",
                  ESP.getFreeHeap(), (unsigned long)millis()); Serial.flush();
#endif
    bt_uart_service_deinit();

    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;

    /* BT/WiFi 互斥：BT 关闭后恢复之前被关闭的 WiFi */
    if (g_wifi_was_on) {
        wifi_mgr_enable();
        g_wifi_was_on = false;
    }
}

/* ═══ 异步请求接口（供 UI 任务调用） ═══ */

void bt_mgr_request_enable(void)
{
#if BT_MGR_DBG_ENABLED
    Serial.printf("[BT-DBG] bt_mgr_request_enable() from core=%d ms=%lu\n",
                  (int)xPortGetCoreID(), (unsigned long)millis()); Serial.flush();
#endif
    g_enable_requested = true;
    g_disable_requested = false;  /* 互斥：取消待处理的禁用请求 */
}

void bt_mgr_request_disable(void)
{
#if BT_MGR_DBG_ENABLED
    Serial.printf("[BT-DBG] bt_mgr_request_disable() from core=%d ms=%lu\n",
                  (int)xPortGetCoreID(), (unsigned long)millis()); Serial.flush();
#endif
    g_disable_requested = true;
    g_enable_requested = false;   /* 互斥：取消待处理的启用请求 */
}

/**
 * @brief  处理待处理的启用/禁用请求
 * @note   **必须在 Arduino loop() 中调用**，与实际 BluetoothSerial API
 *         调用在同一 FreeRTOS 任务上下文中。
 */
void bt_mgr_process_requests(void)
{
    if (g_disable_requested) {
#if BT_MGR_DBG_ENABLED
        Serial.printf("[BT-DBG] processing disable request free_heap=%u\n",
                      ESP.getFreeHeap()); Serial.flush();
#endif
        g_disable_requested = false;
        if (g_bt_enabled) {
            bt_mgr_disable();
        }
    }
    if (g_enable_requested) {
#if BT_MGR_DBG_ENABLED
        Serial.printf("[BT-DBG] processing enable request free_heap=%u ms=%lu\n",
                      ESP.getFreeHeap(), (unsigned long)millis());
        Serial.flush();
#endif
        g_enable_requested = false;
        if (!g_bt_enabled) {
            bt_mgr_enable();
        }
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
        bt_mgr_request_enable();
    } else {
        bt_mgr_request_disable();
    }
}

extern "C" void bt_mgr_task_main(void *arg)
{
    (void)arg;
    kern_poll_loop(bt_mgr_update, 50);
}

#endif /* NATIVE_TEST */
