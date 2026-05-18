#include "hal_system.h"
#ifdef NATIVE_TEST
#include <chrono>
static auto g_start_time = std::chrono::steady_clock::now();
void hal_system_init(void) { g_start_time = std::chrono::steady_clock::now(); }
uint32_t hal_get_ticks(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
}
void hal_delay_ms(uint32_t ms) {}
#else
#include <Arduino.h>
void hal_system_init(void) {}
uint32_t hal_get_ticks(void) { return millis(); }
void hal_delay_ms(uint32_t ms) { delay(ms); }
#endif
