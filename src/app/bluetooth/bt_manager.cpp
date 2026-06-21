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
 *          ═══ BT/WiFi 互斥锁（标准模板，后续开发者请遵循） ═══
 *          由于 ESP32 的 BT (Bluedroid) 与 WiFi 共享同一射频和内存池，
 *          同时开启两者会导致堆内存不足与射频冲突。BT 与 WiFi 采用
 *          对称互斥锁模式：任一方启用时自动关闭另一方，禁用时恢复。
 *
 *          启用 BT 流程：
 *              1. 检查当前堆内存 >= 50KB（BT 初始化约需 55KB）
 *              2. 保存 WiFi 状态 (g_wifi_was_on)
 *              3. 若 WiFi 开启 → wifi_mgr_disable()
 *              4. 等待 WiFi 内存释放（最多 1500ms）
 *              5. bt_uart_service_init() 初始化 Bluedroid SPP
 *              6. 成功后标记 g_bt_enabled = true
 *
 *          禁用 BT 流程：
 *              1. bt_uart_service_deinit() 关闭 BT 栈
 *              2. 若 g_wifi_was_on → wifi_mgr_enable() 恢复 WiFi
 *              3. 标记 g_bt_enabled = false
 *
 *          WiFi 侧对称实现：wifi_mgr_enable() 保存 g_bt_was_on，
 *          自动关闭 BT；wifi_mgr_disable() 恢复 BT。
 *
 *          任何需要 BT 的 App（串口监视器、烧录器透传模式等）
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
bt_mgr_err_t bt_mgr_enable(void) { g_bt_enabled_test = true; return BT_MGR_OK; }
void bt_mgr_disable(void) { g_bt_enabled_test = false; }
void bt_mgr_request_enable(void) { g_bt_enabled_test = true; }
void bt_mgr_request_disable(void) { g_bt_enabled_test = false; }
void bt_mgr_process_requests(void) {}
bool bt_mgr_is_waiting_input(void) { return false; }
bool bt_mgr_is_enabled(void) { return g_bt_enabled_test; }
bool bt_mgr_is_driver_on(void) { return g_bt_enabled_test; }
uint32_t bt_mgr_needed_heap(void) { return 70000; }
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
#include "app/app_mem.h"

extern "C" {
#include "app/wifi/wifi_manager.h"
#include "kernel/kern_task.h"
}

#include "app/app_state.h"

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

/* 内存守卫阈值
 * 本轮重构修复 FreeRTOS 栈单位错误后释放约 36KB，BT 初始化的实际需求
 * 从 ~92KB 降到 ~55KB。空闲低于此值时拒绝启用，避免初始化到一半失败
 * 导致 Bluedroid 处于不一致状态。 */
#define BT_MIN_FREE_HEAP        50000
#define BT_MIN_MAX_ALLOC_HEAP   28000
#define BT_WIFI_SHUTDOWN_TIMEOUT_MS 5000
#define BT_WIFI_SHUTDOWN_POLL_MS    100
#define BT_ENABLE_MAX_RETRIES       3
#define BT_ENABLE_RETRY_DELAY_MS    500

bool bt_mgr_is_driver_on(void) { return g_bt_enabled; }

uint32_t bt_mgr_needed_heap(void) { return BT_MIN_FREE_HEAP; }

bt_mgr_err_t bt_mgr_enable(void) {
    if (g_bt_enabled) return BT_MGR_OK;

    Serial.printf("[BT] enable start free=%u max=%u\n",
                  (uint32_t)ESP.getFreeHeap(), (uint32_t)ESP.getMaxAllocHeap());
    Serial.flush();

#if BT_MGR_DBG_ENABLED
    Serial.printf("[BT-DBG] bt_mgr_enable() entry free_heap=%u ms=%lu\n",
                  ESP.getFreeHeap(), (unsigned long)millis()); Serial.flush();
#endif

    g_wifi_was_on = wifi_mgr_is_enabled();
    if (g_wifi_was_on) {
        wifi_mgr_disable();
    }

    bt_mgr_err_t result = BT_MGR_OK;
    for (int retry = 0; retry <= BT_ENABLE_MAX_RETRIES; retry++) {
        if (retry > 0) {
            /* 仅当刚刚关闭 WiFi 时才重试等待内存释放；
             * 若 WiFi 原本就未开启，内存不会凭空增加，直接失败。 */
            if (!g_wifi_was_on) {
                result = BT_MGR_ERR_HEAP;
                break;
            }
            Serial.printf("[BT] enable retry %d/%d after delay\n",
                          retry, BT_ENABLE_MAX_RETRIES);
            delay(BT_ENABLE_RETRY_DELAY_MS);
        }

        /* ═══ 双内存守卫 ═══
         * Bluedroid Classic BT SPP 初始化 + 连接建链约需 ~55KB。
         * 使用 xeros_mem_can_alloc() 统一检查总空闲、最大连续块与保留水位。
         * WiFi 关闭后内存释放是异步的，因此用重试等待。 */
        uint32_t wait_start = millis();
        while (millis() - wait_start < BT_WIFI_SHUTDOWN_TIMEOUT_MS) {
            if (xeros_mem_can_alloc(BT_MIN_FREE_HEAP, BT_MIN_MAX_ALLOC_HEAP)) break;
            delay(BT_WIFI_SHUTDOWN_POLL_MS);
        }

        if (!xeros_mem_can_alloc(BT_MIN_FREE_HEAP, BT_MIN_MAX_ALLOC_HEAP)) {
            if (g_wifi_was_on && retry < BT_ENABLE_MAX_RETRIES) {
                Serial.printf("[BT] heap guard not met, will retry\n");
                continue;
            }
            kern_kmem_stat_t st;
            xeros_mem_get_stats(&st);
            Serial.printf("[BT] heap guard failed: free=%u max_alloc=%u reserved=%u\n",
                          (uint32_t)st.free_bytes,
                          (uint32_t)st.largest_free_block,
                          (uint32_t)kern_kmem_reserved_bytes());
            result = BT_MGR_ERR_HEAP;
            break;
        }

        bt_uart_err_t uerr = bt_uart_service_init();
        if (uerr == BT_UART_OK) {
            g_bt_enabled = true;
            g_state = BT_MGR_ENABLED;
            Serial.printf("[BT] enable done (attempt %d) free=%u max=%u\n",
                          retry + 1,
                          (uint32_t)ESP.getFreeHeap(),
                          (uint32_t)ESP.getMaxAllocHeap());
            Serial.flush();
#if BT_MGR_DBG_ENABLED
            Serial.printf("[BT-DBG] bt_mgr_enable() done free_heap=%u ms=%lu\n",
                          ESP.getFreeHeap(), (unsigned long)millis()); Serial.flush();
#endif
            return BT_MGR_OK;
        }

        Serial.printf("[BT] bt_uart_service_init failed: %d (attempt %d)\n",
                      (int)uerr, retry + 1);

        if (uerr == BT_UART_ERR_HEAP && retry < BT_ENABLE_MAX_RETRIES) {
            /* 可能是内存尚未完全回收，下一轮重试 */
            continue;
        }

        /* 非内存类错误不再重试 */
        switch (uerr) {
        case BT_UART_ERR_HEAP:       result = BT_MGR_ERR_HEAP; break;
        case BT_UART_ERR_RADIO:      result = BT_MGR_ERR_RADIO; break;
        case BT_UART_ERR_BLUEDROID:  result = BT_MGR_ERR_BLUEDROID; break;
        default:                     result = BT_MGR_ERR_UNKNOWN; break;
        }
        break;
    }

    /* 所有重试失败：恢复 WiFi（如果原本开启），回写状态 */
    if (g_wifi_was_on) {
        wifi_mgr_enable();
        g_wifi_was_on = false;
    }
    g_bt_on = false;   /* A9 */
    return result;
}

void bt_mgr_disable(void) {
    Serial.printf("[BT] disable start free=%u max=%u\n",
                  (uint32_t)ESP.getFreeHeap(), (uint32_t)ESP.getMaxAllocHeap());
    Serial.flush();
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

    Serial.printf("[BT] disable done free=%u max=%u\n",
                  (uint32_t)ESP.getFreeHeap(), (uint32_t)ESP.getMaxAllocHeap());
    Serial.flush();
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
