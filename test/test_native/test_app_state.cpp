/**
 * @file   test_app_state.cpp
 * @brief  App 全局状态单元测试
 * @details 验证 app_state.c 中全局状态的默认值和外部可见性。
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/app_state.h"
}

TEST(AppStateTest, WifiDefaultOn)
{
    EXPECT_TRUE(g_wifi_on);
}

TEST(AppStateTest, BtDefaultOff)
{
    EXPECT_FALSE(g_bt_on);
}
