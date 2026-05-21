/**
 * @file   hal_system.cpp
 * @brief  HAL 系统层实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：使用 std::chrono 提供高精度时间
 *          - 硬件环境时：使用 Arduino millis() / delay()
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_system.h"

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：std::chrono 实现 ═══ */

#include <chrono>

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
uint32_t hal_get_ticks(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
}

/**
 * @brief 延时（native 环境空操作，由测试框架控制）
 */
void hal_delay_ms(uint32_t ms) {
    (void)ms;
}

#else

/* ═══ 硬件环境：Arduino 实现 ═══ */

#include <Arduino.h>

/**
 * @brief 初始化系统（硬件环境无需额外操作）
 */
void hal_system_init(void) {
}

/**
 * @brief 获取系统启动后的毫秒数
 */
uint32_t hal_get_ticks(void) {
    return millis();
}

/**
 * @brief 延时指定的毫秒数
 */
void hal_delay_ms(uint32_t ms) {
    delay(ms);
}

#endif
