/**
 * @file   test_ui_dispatch.cpp
 * @brief  UI 类型派发表行为测试
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_item.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
}

static bool s_switch_callback_fired = false;
static bool s_button_callback_fired = false;
static bool s_destroy_called = false;

extern "C" void switch_exit_callback(void *ud)
{
    (void)ud;
    s_switch_callback_fired = true;
}

extern "C" void button_callback(void *ud)
{
    (void)ud;
    s_button_callback_fired = true;
}

extern "C" void test_destroy_cb(void *ud)
{
    (void)ud;
    s_destroy_called = true;
}

class UiDispatchTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        s_switch_callback_fired = false;
        s_button_callback_fired = false;
        s_destroy_called = false;
        hal_system_init();
        hal_display_init();
    }
};

/**
 * @brief switch_item 进入时翻转值并触发回调
 */
TEST_F(UiDispatchTest, SwitchEnterTogglesValueAndFiresCallback)
{
    bool value = false;
    xerintosh_list_item_t *item = xerintosh_new_switch_item(
        "sw", &value, NULL, switch_exit_callback, switch_icon);
    ASSERT_NE(item, nullptr);

    xerintosh_dispatch_enter(item);

    EXPECT_TRUE(value);
    EXPECT_TRUE(s_switch_callback_fired);

    free((void *)item->content);
    free(item);
}

/**
 * @brief button_item 进入时触发回调
 */
TEST_F(UiDispatchTest, ButtonEnterFiresCallback)
{
    xerintosh_list_item_t *item = xerintosh_new_button_item(
        "btn", button_callback, plus_icon);
    ASSERT_NE(item, nullptr);

    xerintosh_dispatch_enter(item);

    EXPECT_TRUE(s_button_callback_fired);

    free((void *)item->content);
    free(item);
}

/**
 * @brief slider_item 在编辑模式下 input_next 增加值
 */
TEST_F(UiDispatchTest, SliderInputNextInEditModeIncrementsValue)
{
    int16_t value = 50;
    xerintosh_list_item_t *item = xerintosh_new_slider_item(
        "slider", &value, 5, 0, 100, NULL, NULL, slider_icon);
    ASSERT_NE(item, nullptr);

    /* 先进入编辑模式 */
    xerintosh_dispatch_enter(item);
    EXPECT_TRUE(xerintosh_to_slider_item(item)->is_confirmed);

    bool consumed = xerintosh_dispatch_input_next(item);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(value, 55);

    free((void *)item->content);
    free(item);
}

/**
 * @brief slider_item 在未编辑模式下 input_next 不消费输入
 */
TEST_F(UiDispatchTest, SliderInputNextInNormalModeDoesNotConsume)
{
    int16_t value = 50;
    xerintosh_list_item_t *item = xerintosh_new_slider_item(
        "slider", &value, 5, 0, 100, NULL, NULL, slider_icon);
    ASSERT_NE(item, nullptr);

    bool consumed = xerintosh_dispatch_input_next(item);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(value, 50);

    free((void *)item->content);
    free(item);
}

/**
 * @brief slider_item 在编辑模式下 input_exit 恢复备份值
 */
TEST_F(UiDispatchTest, SliderInputExitRestoresBackupValue)
{
    int16_t value = 50;
    xerintosh_list_item_t *item = xerintosh_new_slider_item(
        "slider", &value, 5, 0, 100, NULL, NULL, slider_icon);
    ASSERT_NE(item, nullptr);

    xerintosh_dispatch_enter(item);
    value = 60; /* 模拟用户修改 */

    bool consumed = xerintosh_dispatch_input_exit(item);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(value, 50);
    EXPECT_FALSE(xerintosh_to_slider_item(item)->is_confirmed);

    free((void *)item->content);
    free(item);
}

/**
 * @brief measure 对 switch/slider 返回全宽，对 list/button/user 返回文本宽
 */
TEST_F(UiDispatchTest, MeasureReturnsFullWidthForSwitchAndSlider)
{
    bool sw_value = false;
    int16_t sl_value = 0;
    xerintosh_list_item_t *sw = xerintosh_new_switch_item(
        "switch", &sw_value, NULL, NULL, switch_icon);
    xerintosh_list_item_t *sl = xerintosh_new_slider_item(
        "slider", &sl_value, 1, 0, 10, NULL, NULL, slider_icon);
    xerintosh_list_item_t *list = xerintosh_new_list_item("list", list_icon);

    ASSERT_NE(sw, nullptr);
    ASSERT_NE(sl, nullptr);
    ASSERT_NE(list, nullptr);

    EXPECT_EQ(xerintosh_dispatch_measure(sw), SCREEN_WIDTH - 18);
    EXPECT_EQ(xerintosh_dispatch_measure(sl), SCREEN_WIDTH - 18);
    EXPECT_GT(xerintosh_dispatch_measure(list), 0);
    EXPECT_LT(xerintosh_dispatch_measure(list), SCREEN_WIDTH - 18);

    free((void *)sw->content); free(sw);
    free((void *)sl->content); free(sl);
    free((void *)list->content); free(list);
}

/**
 * @brief has_right_control 仅对 switch/slider 返回 true
 */
TEST_F(UiDispatchTest, HasRightControlOnlyForSwitchAndSlider)
{
    bool sw_value = false;
    int16_t sl_value = 0;
    xerintosh_list_item_t *sw = xerintosh_new_switch_item(
        "switch", &sw_value, NULL, NULL, switch_icon);
    xerintosh_list_item_t *sl = xerintosh_new_slider_item(
        "slider", &sl_value, 1, 0, 10, NULL, NULL, slider_icon);
    xerintosh_list_item_t *list = xerintosh_new_list_item("list", list_icon);
    xerintosh_list_item_t *btn = xerintosh_new_button_item("btn", NULL, plus_icon);
    xerintosh_list_item_t *user = xerintosh_new_user_item("user", NULL, NULL, NULL, user_icon);

    ASSERT_NE(sw, nullptr);
    ASSERT_NE(sl, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_NE(btn, nullptr);
    ASSERT_NE(user, nullptr);

    EXPECT_TRUE(xerintosh_dispatch_has_right_control(sw));
    EXPECT_TRUE(xerintosh_dispatch_has_right_control(sl));
    EXPECT_FALSE(xerintosh_dispatch_has_right_control(list));
    EXPECT_FALSE(xerintosh_dispatch_has_right_control(btn));
    EXPECT_FALSE(xerintosh_dispatch_has_right_control(user));

    free((void *)sw->content); free(sw);
    free((void *)sl->content); free(sl);
    free((void *)list->content); free(list);
    free((void *)btn->content); free(btn);
    free((void *)user->content); free(user);
}

/**
 * @brief destroy 调用 user_item 的 destroy_callback
 */
TEST_F(UiDispatchTest, DestroyCallsUserItemDestroyCallback)
{
    int dummy_data = 42;
    xerintosh_list_item_t *item = xerintosh_new_user_item(
        "user", NULL, NULL, NULL, user_icon);
    ASSERT_NE(item, nullptr);

    item->user_data = &dummy_data;
    xerintosh_to_user_item(item)->destroy_callback = test_destroy_cb;
    xerintosh_dispatch_destroy(item);

    EXPECT_TRUE(s_destroy_called);

    free((void *)item->content);
    free(item);
}
