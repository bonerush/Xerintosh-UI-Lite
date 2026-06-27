#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_types.h"
#include "hal/hal_display.h"
}

/* ═══ xerintosh_is_item_visible() 边界测试 ═══ */
/*
 * 函数实现（src/ui/ui_draw_list.c:24-27）：
 *   return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < HAL_SCREEN_HEIGHT);
 *
 * 常量值（native 测试环境）：
 *   LIST_INFO_BAR_HEIGHT = 3
 *   HAL_SCREEN_HEIGHT        = 160
 *
 * 可见范围：_y_item 在 [2, 161] 闭区间内
 * 不可见：  _y_item <= 1 或 _y_item >= 162
 */

TEST(UiItemTest, IsItemVisibleInsideBounds)
{
    /* 项在可视区域内：明显高于 info bar 但未超出屏幕 */
    EXPECT_TRUE(xerintosh_is_item_visible(LIST_INFO_BAR_HEIGHT + 2)); /* y=5 */
    EXPECT_TRUE(xerintosh_is_item_visible(HAL_SCREEN_HEIGHT - 3));        /* y=157 */
}

TEST(UiItemTest, IsItemVisibleOutsideBounds)
{
    /* 项超出屏幕顶部（y <= 1 不可见） */
    EXPECT_FALSE(xerintosh_is_item_visible(LIST_INFO_BAR_HEIGHT - 2)); /* y=1 */
    /* 项超出屏幕底部（y >= 162 不可见） */
    EXPECT_FALSE(xerintosh_is_item_visible(HAL_SCREEN_HEIGHT + 2));        /* y=162 */
}

TEST(UiItemTest, IsItemVisibleToleranceEdges)
{
    /* 上下各有 2px 容差 */
    /* 顶部边界：y=2 可见（2+2=4>3），y=1 不可见（1+2=3≯3） */
    EXPECT_TRUE(xerintosh_is_item_visible(2));
    EXPECT_FALSE(xerintosh_is_item_visible(1));
    /* 底部边界：y=161 可见（161-2=159<160），y=162 不可见（162-2=160≮160） */
    EXPECT_TRUE(xerintosh_is_item_visible(161));
    EXPECT_FALSE(xerintosh_is_item_visible(162));
}
