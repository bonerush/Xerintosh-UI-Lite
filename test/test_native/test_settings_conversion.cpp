#include <gtest/gtest.h>

extern "C" {
#include "app/settings/settings.h"
}

TEST(SettingsConversionTest, BrightnessLevelToHw)
{
    g_brightness_level = 5;
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 5),
              settings_brightness_hw_value());
}

TEST(SettingsConversionTest, AnimSpeedLevelToHw)
{
    g_anim_speed_level = 5;
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 5),
              settings_anim_speed_value());
}

TEST(SettingsConversionTest, BaudLevelToHw)
{
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BAUD_RATE, 5), 115200);
}

TEST(SettingsConversionTest, SpringStiffnessLevelToHw)
{
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_SPRING_STIFFNESS, 5),
              (int32_t)(settings_spring_stiffness_hw_value(5) * 10000.0f));
}

TEST(SettingsConversionTest, HwToBrightnessLevel)
{
    EXPECT_EQ(settings_hw_to_level(SETTINGS_KIND_BRIGHTNESS, 128), 5);
}
