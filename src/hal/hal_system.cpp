/**
 * @file   hal_system.cpp
 * @brief  HAL 系统层实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：使用 std::chrono 提供高精度时间
 *          - 硬件环境时：使用 ESP-IDF esp_timer_get_time() / vTaskDelay()
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_system.h"

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：std::chrono 实现 ═══ */

#include <chrono>
#include <thread>

static auto g_start_time = std::chrono::steady_clock::now();  /* 系统启动时间基准 */

/**
 * @brief 初始化系统时间基准
 */
void hal_system_init(void) {
    g_start_time = std::chrono::steady_clock::now();
}

/**
 * @brief 获取系统启动后的毫秒数
 */
uint32_t hal_get_ticks_ms(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
}

/**
 * @brief 延时指定的毫秒数
 */
void hal_delay_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#else

/* ═══ 硬件环境：ESP-IDF 实现 ═══ */

#include "esp_timer.h"
#include "kernel/kern_smp.h"
#include "kernel/kern_task.h"
#include "esp32/rom/ets_sys.h"

/**
 * @brief 初始化系统（ESP-IDF 自动完成启动，此处留空兼容）
 */
void hal_system_init(void) {
}

/**
 * @brief 获取系统启动后的毫秒数
 */
uint32_t hal_get_ticks_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief 延时指定的毫秒数
 *
 * Xeros 启动后（g_current_task != NULL）使用 kern_sleep_ms 让出 CPU；
 * 启动早期（g_current_task == NULL）使用 ROM 忙等延时，不依赖 FreeRTOS。
 */
void hal_delay_ms(uint32_t ms) {
    if (g_current_task != NULL) {
        kern_sleep_ms(ms);
    } else {
        ets_delay_us(ms * 1000);
    }
}

#endif
