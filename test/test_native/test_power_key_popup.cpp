/**
 * @file   test_power_key_popup.cpp
 * @brief  电源键弹窗模块单元测试
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/shutdown/power_key_popup.h"
}

/* ═══ 测试夹具 ═══ */

class PowerKeyPopupTest : public ::testing::Test {
protected:
    void SetUp() override {
        power_key_popup_init();
    }
};

/* ═══ 初始化测试 ═══ */

TEST_F(PowerKeyPopupTest, InitClearsDualActive)
{
    EXPECT_FALSE(power_key_popup_is_dual_active());
    EXPECT_FALSE(power_key_popup_is_visible());
}

TEST_F(PowerKeyPopupTest, UpdateDoesNotCrashWhenNoButtonsPressed)
{
    power_key_popup_update();
    SUCCEED();
}

TEST_F(PowerKeyPopupTest, DualActiveIsFalseAfterInitAndUpdate)
{
    power_key_popup_update();
    EXPECT_FALSE(power_key_popup_is_dual_active());
    EXPECT_FALSE(power_key_popup_is_visible());
}

/* ═══ 旧测试（保留兼容性） ═══ */

TEST(PowerKeyPopupLegacyTest, InitDoesNotCrash)
{
    power_key_popup_init();
    SUCCEED();
}

TEST(PowerKeyPopupLegacyTest, InitiallyNotVisible)
{
    power_key_popup_init();
    EXPECT_FALSE(power_key_popup_is_visible());
}

TEST(PowerKeyPopupLegacyTest, UpdateDoesNotCrash)
{
    power_key_popup_init();
    power_key_popup_update();
    SUCCEED();
}
