#include <gtest/gtest.h>

extern "C" {
#include "app/settings/settings.h"
}

TEST(SettingsConversionTest, BrightnessLevelToHw)
{
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 1), 25);   /* 1*10*255/100 = 25 */
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 5), 127);  /* 5*10*255/100 = 127 */
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 10), 255); /* 10*10*255/100 = 255 */

    /* 参数与全局变量解耦：即使全局不同，结果仍由传入 level 决定 */
    g_brightness_level = 1;
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 10), 255);
}

TEST(SettingsConversionTest, AnimSpeedLevelToHw)
{
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 1), 45);  /* 40 + 1*5 */
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 5), 65);  /* 40 + 5*5 */
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 10), 90); /* 40 + 10*5 */

    /* 参数与全局变量解耦 */
    g_anim_speed_level = 1;
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 10), 90);
}

TEST(SettingsConversionTest, BrightnessLevelToHwClampsOutOfRange)
{
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 0), 25);  /* clamp to 1 */
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 11), 255); /* clamp to 10 */
}

TEST(SettingsConversionTest, AnimSpeedLevelToHwClampsOutOfRange)
{
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 0), 45);  /* clamp to 1 */
    EXPECT_EQ(settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 11), 90); /* clamp to 10 */
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
