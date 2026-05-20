#ifdef NATIVE_TEST

#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "ui/ui_draw_driver.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

/* 桩回调（供 native 测试链接 app_init.c 使用） */
void on_brightness_change_cb(void) {}
void on_anim_speed_change_cb(void) {}
void on_anim_enabled_change_cb(void) {}
void on_screen_rotation_change_cb(void) {}

/* 外部状态标志桩 */
bool wifi_on = true;
bool bt_on = true;
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

#endif
