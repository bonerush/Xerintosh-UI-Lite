/**
 * @file   test_ui_item_null_value.cpp
 * @brief  验证 switch/slider 创建时拒绝 NULL value 指针
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_item.h"
}

TEST(UiItemNullValueTest, SwitchNullValueReturnsNull)
{
    xerintosh_list_item_t *item = xerintosh_new_switch_item(
        "test", NULL, NULL, NULL, default_icon);
    EXPECT_EQ(item, nullptr);
}

TEST(UiItemNullValueTest, SliderNullValueReturnsNull)
{
    int16_t placeholder = 0;
    xerintosh_list_item_t *item = xerintosh_new_slider_item(
        "test", NULL, 1, 0, 10, NULL, NULL, default_icon);
    EXPECT_EQ(item, nullptr);

    /* 正常 value 指针应成功创建 */
    item = xerintosh_new_slider_item(
        "test", &placeholder, 1, 0, 10, NULL, NULL, default_icon);
    EXPECT_NE(item, nullptr);
}
