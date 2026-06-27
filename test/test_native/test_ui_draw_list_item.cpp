/**
 * @file   test_ui_draw_list_item.cpp
 * @brief  UI 列表项绘制拆分回归测试
 * @details 验证 xerintosh_draw_list 在空根、单列表项、带图标项下不崩溃。
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_context.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "ui/ui_dirty.h"
#include "ui/ui_drawer.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
}

class UiDrawListItemTest : public ::testing::Test {
protected:
    void SetUp() override {
        xerintosh_context_init();
        hal_system_init();
        hal_display_init();
        xerintosh_clear_children_of_list(xerintosh_get_root_list());
    }

    void TearDown() override {
        xerintosh_clear_children_of_list(xerintosh_get_root_list());
    }
};

TEST_F(UiDrawListItemTest, DrawListDoesNotCrashOnEmptyRoot)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_clear_children_of_list(root);
    EXPECT_NO_FATAL_FAILURE(xerintosh_draw_list());
}

TEST_F(UiDrawListItemTest, DrawListRendersSingleVisibleItem)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item("A", list_icon);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));

    xerintosh_init_core();
    g_in_xerintosh = true;

    EXPECT_NO_FATAL_FAILURE(xerintosh_draw_list());
}

TEST_F(UiDrawListItemTest, DrawListRendersItemWithCustomBitmap)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    static const uint8_t dummy_bits[8] = {0};
    xerintosh_list_item_t *a = xerintosh_new_list_item("Bitmap", custom_icon);
    ASSERT_NE(a, nullptr);
    a->bitmap_data = dummy_bits;
    a->bitmap_w = 8;
    a->bitmap_h = 8;
    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));

    xerintosh_init_core();
    g_in_xerintosh = true;

    EXPECT_NO_FATAL_FAILURE(xerintosh_draw_list());
}

TEST_F(UiDrawListItemTest, DrawListRendersLongScrollingText)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item(
        "VeryLongTextThatShouldTriggerScrollingEventually", list_icon);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));

    xerintosh_init_core();
    g_in_xerintosh = true;

    EXPECT_NO_FATAL_FAILURE(xerintosh_draw_list());
}
