#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "ui/ui_draw_driver.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
}

TEST(AnimationTest, EasingConverges)
{
    float pos = 0.0f;
    float target = 100.0f;
    for (int i = 0; i < 200; i++) {
        astra_animation(&pos, target, 92.0f);
    }
    EXPECT_FLOAT_EQ(pos, target);
}

TEST(ItemTest, RootListCreated)
{
    astra_list_item_t* root = astra_get_root_list();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->type, list_item);
}

TEST(ItemTest, PushItem)
{
    astra_list_item_t* root = astra_get_root_list();
    astra_list_item_t* item = astra_new_list_item("Test", default_icon);
    bool result = astra_push_item_to_list(root, item);
    EXPECT_TRUE(result);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    hal_system_init();
    hal_display_init();
    astra_ui_driver_init();
    return RUN_ALL_TESTS();
}
