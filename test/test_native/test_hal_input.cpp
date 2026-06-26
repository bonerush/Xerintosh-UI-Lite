/**
 * @file   test_hal_input.cpp
 * @brief  HAL input layer unit tests
 * @details Verify per-button double-click switch and input context behavior.
 */

#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_input.h"
}

TEST(HalInputTest, DoubleClickCanBeEnabledPerButton)
{
    hal_input_init();
    hal_input_set_double_click_enabled_for_button(HAL_BTN_A, true);
    hal_input_set_double_click_enabled_for_button(HAL_BTN_B, false);

    EXPECT_TRUE(hal_input_get_double_click_enabled_for_button(HAL_BTN_A));
    EXPECT_FALSE(hal_input_get_double_click_enabled_for_button(HAL_BTN_B));

    /* 旧全局开关仍应保持向后兼容 */
    hal_input_set_double_click_enabled(true);
    EXPECT_TRUE(hal_input_get_double_click_enabled_for_button(HAL_BTN_A));
    EXPECT_TRUE(hal_input_get_double_click_enabled_for_button(HAL_BTN_B));
}
