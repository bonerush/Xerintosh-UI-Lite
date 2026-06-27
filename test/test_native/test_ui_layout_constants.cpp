/**
 * @file   test_ui_layout_constants.cpp
 * @brief  UI 布局常量存在性与值域测试
 * @details 验证阶段 1 诊断中识别的魔法数字已被提取为命名常量。
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_types.h"
}

TEST(UiLayoutConstants, ListDecorationConstantsExist)
{
    EXPECT_GT(LIST_DECO_H_LINE1_LEN, 0);
    EXPECT_GT(LIST_DECO_H_LINE2_LEN, 0);
    EXPECT_EQ(LIST_DECO_H_LINE2_LEN, LIST_DECO_H_LINE1_LEN + 1);
}

TEST(UiLayoutConstants, ScrollbarConstantsArePositive)
{
    EXPECT_GT(SCROLLBAR_WIDTH, 0);
    EXPECT_GT(SCROLLBAR_TRACK_ENDCAP_H, 0);
    EXPECT_GT(SCROLLBAR_TRACK_X_OFFSET, 0);
    EXPECT_GT(SCROLLBAR_LEFT_BORDER_OFFSET, 0);
    EXPECT_GT(SCROLLBAR_RIGHT_BORDER_OFFSET, 0);
}

TEST(UiLayoutConstants, SelectorConstantsAreDefined)
{
    EXPECT_GT(SELECTOR_HEIGHT, 0);
    EXPECT_GT(SELECTOR_DASH_EXTEND, 0);
}

TEST(UiLayoutConstants, LongPressHintConstantsArePositive)
{
    EXPECT_GT(LONG_PRESS_HINT_BAR_W, 0);
    EXPECT_GT(LONG_PRESS_HINT_BAR_H, 0);
    EXPECT_GE(LONG_PRESS_HINT_MARGIN_X, 0);
    EXPECT_GE(LONG_PRESS_HINT_MARGIN_Y, 0);
}

TEST(UiLayoutConstants, TextScrollCycleIsNonZero)
{
    EXPECT_GT(TEXT_SCROLL_CYCLE_MS, 0u);
}

TEST(UiLayoutConstants, SpringDtScaleIsPositive)
{
    EXPECT_GT(SPRING_DT_SCALE, 0.0f);
}
