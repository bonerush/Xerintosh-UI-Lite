/**
 * @file   test_hal_system.cpp
 * @brief  HAL system layer unit tests
 * @details Verify native hal_delay_ms semantics with real sleep.
 */

#include <gtest/gtest.h>
#include <chrono>

extern "C" {
#include "hal/hal_system.h"
}

TEST(HalSystemDelayTest, DelayAtLeast50ms)
{
    auto start = std::chrono::steady_clock::now();
    hal_delay_ms(50);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_GE(elapsed_ms, 45);
}

TEST(HalSystemDelayTest, DelayZeroDoesNotCrash)
{
    hal_delay_ms(0);
}
