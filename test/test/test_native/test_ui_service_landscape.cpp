/**
 * @file   test_ui_service_landscape.cpp
 * @brief  ui_service 横屏 helper 单元测试
 * @details 验证 ui_service_enter_landscape / ui_service_exit_landscape
 *          能正确保存并恢复屏幕方向状态。
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/ui_service.h"
#include "app/settings/settings.h"
#include "hal/hal_display.h"
}

TEST(UiServiceLandscape, EnterLandscape_FromPortrait_SwitchesToLandscape)
{
    /* 初始竖屏 */
    g_is_landscape = false;
    g_screen_rotation_level = ORIENTATION_PORTRAIT;
    hal_display_set_rotation(0);

    ui_service_enter_landscape();

    EXPECT_TRUE(g_is_landscape);
    EXPECT_EQ(g_screen_rotation_level, ORIENTATION_LANDSCAPE);
    EXPECT_EQ(hal_display_get_rotation(), 1);
}

TEST(UiServiceLandscape, ExitLandscape_RestoresPortrait)
{
    /* 从竖屏进入 */
    g_is_landscape = false;
    g_screen_rotation_level = ORIENTATION_PORTRAIT;
    hal_display_set_rotation(0);

    ui_service_enter_landscape();
    ASSERT_TRUE(g_is_landscape);
    ASSERT_EQ(hal_display_get_rotation(), 1);

    ui_service_exit_landscape();

    EXPECT_FALSE(g_is_landscape);
    EXPECT_EQ(g_screen_rotation_level, ORIENTATION_PORTRAIT);
    EXPECT_EQ(hal_display_get_rotation(), 0);
}

TEST(UiServiceLandscape, EnterLandscape_AlreadyLandscape_NoChange)
{
    /* 初始已是横屏 */
    g_is_landscape = true;
    g_screen_rotation_level = ORIENTATION_LANDSCAPE;
    hal_display_set_rotation(1);

    ui_service_enter_landscape();

    EXPECT_TRUE(g_is_landscape);
    EXPECT_EQ(g_screen_rotation_level, ORIENTATION_LANDSCAPE);
    EXPECT_EQ(hal_display_get_rotation(), 1);
}
