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
 * @brief 简单状态机：一次短按立即返回 SHORT_PRESS（无 300ms 延迟）
 */
TEST(DoubleClickSwitchTest, SimpleModeSingleShortPress)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 按下 */
    EXPECT_EQ(hal_input_simple_process(&st, true, false, 0), HAL_EVENT_NONE);
    /* 释放 → 立即返回 SHORT_PRESS */
    EXPECT_EQ(hal_input_simple_process(&st, false, true, 100), HAL_EVENT_SHORT_PRESS);

    /* 之后不应重复返回 */
    EXPECT_EQ(hal_input_simple_process(&st, false, false, 200), HAL_EVENT_NONE);
}

/**
 * @brief 简单状态机：快速双击也只返回两次独立 SHORT_PRESS（不产生 DOUBLE_CLICK）
 */
TEST(DoubleClickSwitchTest, SimpleModeFastDoublePressReturnsTwoShorts)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次短按 */
    EXPECT_EQ(hal_input_simple_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_simple_process(&st, false, true, 100), HAL_EVENT_SHORT_PRESS);

    /* 第二次短按 */
    EXPECT_EQ(hal_input_simple_process(&st, true, false, 150), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_simple_process(&st, false, true, 250), HAL_EVENT_SHORT_PRESS);

    /* 静默帧不应有事件 */
    EXPECT_EQ(hal_input_simple_process(&st, false, false, 500), HAL_EVENT_NONE);
}

/**
 * @brief 简单状态机：长按触发 LONG_PRESS，释放后不返回额外事件
 */
TEST(DoubleClickSwitchTest, SimpleModeLongPress)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 按下 */
    EXPECT_EQ(hal_input_simple_process(&st, true, false, 0), HAL_EVENT_NONE);

    /* 持续按下，达到长按阈值 */
    EXPECT_EQ(hal_input_simple_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);

    /* 释放：不返回额外事件 */
    EXPECT_EQ(hal_input_simple_process(&st, false, true, 600), HAL_EVENT_NONE);
}

/**
 * @brief 简单状态机：长按后紧跟短按，两个事件都正常返回
 */
TEST(DoubleClickSwitchTest, SimpleModeLongPressThenShortPress)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 长按 */
    EXPECT_EQ(hal_input_simple_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_simple_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);
    EXPECT_EQ(hal_input_simple_process(&st, false, true, 600), HAL_EVENT_NONE);

    /* 短按 */
    EXPECT_EQ(hal_input_simple_process(&st, true, false, 700), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_simple_process(&st, false, true, 800), HAL_EVENT_SHORT_PRESS);
}

/**
 * @brief 简单状态机：永远不返回 DOUBLE_CLICK
 */
TEST(DoubleClickSwitchTest, SimpleModeNeverReturnsDoubleClick)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 模拟多次快速按下释放 */
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(hal_input_simple_process(&st, true, false, i * 200), HAL_EVENT_NONE);
        hal_event_t ev = hal_input_simple_process(&st, false, true, i * 200 + 50);
        EXPECT_NE(ev, HAL_EVENT_DOUBLE_CLICK);
        EXPECT_EQ(ev, HAL_EVENT_SHORT_PRESS);
    }
}

/**
 * @brief 双击状态机回归测试：窗口期内双击返回 DOUBLE_CLICK
 */
TEST(DoubleClickSwitchTest, DCModeDoubleClickWorks)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次短按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期内第二次短按 → 双击 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 150), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 250), HAL_EVENT_DOUBLE_CLICK);
}

/**
 * @brief 双击状态机回归：长按优先于双击
 */
TEST(DoubleClickSwitchTest, DCModeLongPressPriority)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 长按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 600), HAL_EVENT_NONE);
}

/**
 * @brief 双击状态机回归：单次短按在窗口期超时后返回
 */
TEST(DoubleClickSwitchTest, DCModeSingleShortPress)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 按下释放 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期超时 → 短按 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 401), HAL_EVENT_SHORT_PRESS);
}
