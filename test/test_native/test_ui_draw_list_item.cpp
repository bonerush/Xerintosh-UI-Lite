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

/*
 * 回归测试：当选中项被相机滚动到屏幕底部边缘时，文字仍应被绘制，
 * 这样后续 XOR 选择器反色才能显示文字。
 *
 * 重构 U3 拆分 xerintosh_draw_list_item 后，item_text_is_visible() 把
 * "文字基线是否可见" 错误地判断为 "文字底部是否可见"，导致底部 4px
 * 内的文字被跳过，选择器内出现空白。
 */
TEST_F(UiDrawListItemTest, DrawsTextForItemNearBottomEdgeAfterCameraScroll)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *item = xerintosh_new_list_item("B", list_icon);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(xerintosh_push_item_to_list(root, item));

    xerintosh_init_core();
    g_in_xerintosh = true;
    g_anim_enabled = false;

    /* 绑定并 snap 动画到当前位置 */
    xerintosh_bind_item_to_selector(item);
    xerintosh_refresh_selector_position();
    xerintosh_refresh_camera_position();
    xerintosh_refresh_list_item_position();

    /* 模拟相机滚动后，文字基线正好在屏幕底部边缘附近（HAL_SCREEN_HEIGHT - 4） */
    int16_t font_h = hal_get_font_height();
    item->y_list_item = item->y_list_item_trg = HAL_SCREEN_HEIGHT - 4;
    g_xerintosh_camera.y_camera = 0.0f;
    g_xerintosh_camera.y_camera_trg = 0.0f;

    hal_display_clear();
    xerintosh_draw_list();

    int16_t text_x = LIST_ITEM_LEFT_MARGIN + 10;
    int16_t text_y = HAL_SCREEN_HEIGHT - 4; /* 文字基线 */
    bool found_fg = false;
    for (int16_t row = text_y - font_h + 1; row <= text_y; ++row) {
        for (int16_t col = text_x; col < text_x + 6; ++col) {
            if (hal_test_fb_read(col, row) == COLOR_FG) {
                found_fg = true;
                break;
            }
        }
        if (found_fg) break;
    }
    EXPECT_TRUE(found_fg)
        << "Text should be drawn when its baseline is within 4px of screen bottom";
}
