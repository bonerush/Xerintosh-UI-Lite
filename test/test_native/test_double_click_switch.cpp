#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_input.h"
#include "hal/hal_input_double_click.h"
}

/* ═══ 双击开关功能测试 ═══ */

/**
 * @brief 默认状态下双击检测为禁用
 */
TEST(DoubleClickSwitchTest, DefaultDisabled)
{
    EXPECT_FALSE(hal_input_is_double_click_enabled());
}

/**
 * @brief 启用/禁用切换正常
 */
TEST(DoubleClickSwitchTest, EnableDisableToggle)
{
    hal_input_set_double_click_enabled(true);
    EXPECT_TRUE(hal_input_is_double_click_enabled());

    hal_input_set_double_click_enabled(false);
    EXPECT_FALSE(hal_input_is_double_click_enabled());
}

/**
 * @brief 禁用时简单状态机不返回 DOUBLE_CLICK
 * @note 模拟一次短按：按下→释放，应立即返回 SHORT_PRESS（无 300ms 延迟）
 */
TEST(DoubleClickSwitchTest, DisabledModeNoDoubleClick)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 禁用双击 */
    hal_input_set_double_click_enabled(false);

    /* 按下 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    /* 释放 → 立即返回 SHORT_PRESS */
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_SHORT_PRESS);

    /* 之后不应重复返回 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 200), HAL_EVENT_NONE);
}

/**
 * @brief 禁用时快速双击也只返回一次 SHORT_PRESS
 */
TEST(DoubleClickSwitchTest, DisabledModeFastDoublePressReturnsShortOnly)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    hal_input_set_double_click_enabled(false);

    /* 第一次短按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_SHORT_PRESS);

    /* 第二次短按（窗口期内） */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 150), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 250), HAL_EVENT_SHORT_PRESS);

    /* 确认不会出现 DOUBLE_CLICK */
    /* 静默帧不应有事件 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_NONE);
}

/**
 * @brief 禁用时长按正常触发
 */
TEST(DoubleClickSwitchTest, DisabledModeLongPress)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    hal_input_set_double_click_enabled(false);

    /* 按下 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);

    /* 持续按下，达到长按阈值 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);

    /* 释放 */
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 600), HAL_EVENT_NONE);
}

/**
 * @brief 启用时行为与现有双击状态机一致（回归测试）
 */
TEST(DoubleClickSwitchTest, EnabledModeDoubleClickWorks)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    hal_input_set_double_click_enabled(true);

    /* 第一次短按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期内第二次短按 → 双击 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 150), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 250), HAL_EVENT_DOUBLE_CLICK);
}

/**
 * @brief 启用时长按优先于双击
 */
TEST(DoubleClickSwitchTest, EnabledModeLongPressPriority)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    hal_input_set_double_click_enabled(true);

    /* 按下 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 600), HAL_EVENT_NONE);
}

/**
 * @brief 启用时单次短按在窗口期超时后返回（回归）
 */
TEST(DoubleClickSwitchTest, EnabledModeSingleShortPress)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    hal_input_set_double_click_enabled(true);

    /* 按下释放 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期超时 → 短按 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 401), HAL_EVENT_SHORT_PRESS);
}
