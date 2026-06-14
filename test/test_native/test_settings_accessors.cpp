/**
 * @file   test_settings_accessors.cpp
 * @brief  Settings 模块 getter/setter 单元测试
 * @details 验证 settings 的 getter/setter 函数正确性。
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/settings/settings.h"
}

/* ═══ 测试夹具 ═══ */

class SettingsAccessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* 重置到已知默认状态 */
        settings_set_brightness(5);
        settings_set_anim_speed(5);
        settings_set_rotation(ORIENTATION_LANDSCAPE);
        settings_set_landscape(true);
        settings_set_baud_rate(5);
    }
};

/* ═══ 亮度 getter/setter ═══ */

TEST_F(SettingsAccessorTest, BrightnessDefaultIsFive)
{
    EXPECT_EQ(settings_get_brightness(), 5);
}

TEST_F(SettingsAccessorTest, BrightnessSetAndGet)
{
    settings_set_brightness(8);
    EXPECT_EQ(settings_get_brightness(), 8);
}

TEST_F(SettingsAccessorTest, BrightnessClampsLow)
{
    settings_set_brightness(0);
    EXPECT_GE(settings_get_brightness(), 1);
}

TEST_F(SettingsAccessorTest, BrightnessClampsHigh)
{
    settings_set_brightness(15);
    EXPECT_LE(settings_get_brightness(), 10);
}

/* ═══ 动画速度 getter/setter ═══ */

TEST_F(SettingsAccessorTest, AnimSpeedDefaultIsFive)
{
    EXPECT_EQ(settings_get_anim_speed(), 5);
}

TEST_F(SettingsAccessorTest, AnimSpeedSetAndGet)
{
    settings_set_anim_speed(3);
    EXPECT_EQ(settings_get_anim_speed(), 3);
}

TEST_F(SettingsAccessorTest, AnimSpeedClampsLow)
{
    settings_set_anim_speed(0);
    EXPECT_GE(settings_get_anim_speed(), 1);
}

TEST_F(SettingsAccessorTest, AnimSpeedClampsHigh)
{
    settings_set_anim_speed(20);
    EXPECT_LE(settings_get_anim_speed(), 10);
}

/* ═══ 屏幕方向 getter/setter ═══ */

TEST_F(SettingsAccessorTest, RotationDefaultIsLandscape)
{
    EXPECT_EQ(settings_get_rotation(), ORIENTATION_LANDSCAPE);
}

TEST_F(SettingsAccessorTest, RotationSetPortrait)
{
    settings_set_rotation(ORIENTATION_PORTRAIT);
    EXPECT_EQ(settings_get_rotation(), ORIENTATION_PORTRAIT);
}

TEST_F(SettingsAccessorTest, RotationClampsInvalidLow)
{
    settings_set_rotation(0);
    EXPECT_EQ(settings_get_rotation(), ORIENTATION_LANDSCAPE);
}

TEST_F(SettingsAccessorTest, RotationClampsInvalidHigh)
{
    settings_set_rotation(99);
    EXPECT_EQ(settings_get_rotation(), ORIENTATION_LANDSCAPE);
}

TEST_F(SettingsAccessorTest, RotationSetPortraitStillWorks)
{
    settings_set_rotation(ORIENTATION_PORTRAIT);
    EXPECT_EQ(settings_get_rotation(), ORIENTATION_PORTRAIT);
}

TEST_F(SettingsAccessorTest, RotationSetLandscapeStillWorks)
{
    settings_set_rotation(ORIENTATION_LANDSCAPE);
    EXPECT_EQ(settings_get_rotation(), ORIENTATION_LANDSCAPE);
}

TEST_F(SettingsAccessorTest, LandscapeDefaultIsTrue)
{
    EXPECT_TRUE(settings_get_landscape());
}

TEST_F(SettingsAccessorTest, LandscapeSetAndGet)
{
    settings_set_landscape(false);
    EXPECT_FALSE(settings_get_landscape());
}

/* ═══ 波特率 getter/setter ═══ */

TEST_F(SettingsAccessorTest, BaudRateDefaultIsFive)
{
    EXPECT_EQ(settings_get_baud_rate(), 5);
}

TEST_F(SettingsAccessorTest, BaudRateSetAndGet)
{
    settings_set_baud_rate(3);
    EXPECT_EQ(settings_get_baud_rate(), 3);
}

TEST_F(SettingsAccessorTest, BaudRateClampsLow)
{
    settings_set_baud_rate(0);
    EXPECT_GE(settings_get_baud_rate(), 1);
}

TEST_F(SettingsAccessorTest, BaudRateClampsHigh)
{
    settings_set_baud_rate(10);
    EXPECT_LE(settings_get_baud_rate(), 6);
}
