/**
 * @file   test_power_key_popup.cpp
 * @brief  电源键弹窗模块单元测试
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/shutdown/power_key_popup.h"
#include "hal/hal_power_key.h"
}

/* ═══ 测试 ═══ */

TEST(PowerKeyPopupTest, InitDoesNotCrash)
{
    power_key_popup_init();
    SUCCEED();
}

TEST(PowerKeyPopupTest, InitiallyNotVisible)
{
    power_key_popup_init();
    EXPECT_FALSE(power_key_popup_is_visible());
}

TEST(PowerKeyPopupTest, UpdateDoesNotCrash)
{
    power_key_popup_init();
    power_key_popup_update();
    SUCCEED();
}
