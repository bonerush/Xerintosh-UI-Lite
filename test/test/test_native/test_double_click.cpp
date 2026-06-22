#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_input.h"
#include "hal/hal_input_double_click.h"
}

/* ═══ Phase 1: 双击检测时序逻辑测试 ═══ */

/**
 * @brief 单次短按：窗口期超时后返回 SHORT_PRESS
 */
TEST(DoubleClickTest, SingleShortPressReturnsShortAfterWindow)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 按下 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    /* 释放 */
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期内没有第二次按下 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 200), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 399), HAL_EVENT_NONE);

    /* 窗口期超时（300ms 窗口：100 + 301 = 401） */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 401), HAL_EVENT_SHORT_PRESS);

    /* 之后不再重复返回 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_NONE);
}

/**
 * @brief 快速双击（在窗口期内）：返回 DOUBLE_CLICK
 */
TEST(DoubleClickTest, DoubleClickWithinWindow)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次按下释放 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期内第二次按下释放 → 双击 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 150), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 250), HAL_EVENT_DOUBLE_CLICK);

    /* 之后状态干净 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 300), HAL_EVENT_NONE);
}

/**
 * @brief 慢速双击（超出窗口期）：返回两个独立的 SHORT_PRESS
 */
TEST(DoubleClickTest, SlowDoubleClickReturnsTwoShortPresses)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次按下释放 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期超时，返回第一次短按 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 401), HAL_EVENT_SHORT_PRESS);

    /* 第二次按下释放 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 500), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 600), HAL_EVENT_NONE);

    /* 第二次的短按也要等窗口期超时 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 901), HAL_EVENT_SHORT_PRESS);
}

/**
 * @brief 长按：达到阈值时返回 LONG_PRESS，释放不重复返回
 */
TEST(DoubleClickTest, LongPressReturnsLong)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 按下 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);

    /* 持续按下，达到长按阈值 500ms */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);

    /* 继续按住，不应重复触发 */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 600), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 1000), HAL_EVENT_NONE);

    /* 释放 */
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 1100), HAL_EVENT_NONE);
}

/**
 * @brief 短按后长按：长按优先级高于双击
 * @note 第一次短按释放后，窗口期内第二次按下并达到长按阈值，
 *       应触发 LONG_PRESS，释放后不应再触发 DOUBLE_CLICK。
 */
TEST(DoubleClickTest, ShortPressThenLongPressReturnsLongNotDoubleClick)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次短按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期内第二次按下 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 150), HAL_EVENT_NONE);

    /* 持续按下达到长按阈值 500ms */
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 650), HAL_EVENT_LONG_PRESS);

    /* 释放，不应触发双击 */
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 700), HAL_EVENT_NONE);
}

/**
 * @brief 长按后再短按：两个事件都应正常返回
 */
TEST(DoubleClickTest, LongPressThenShortPressReturnsBoth)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 长按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 500), HAL_EVENT_LONG_PRESS);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 600), HAL_EVENT_NONE);

    /* 短按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 700), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 800), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 1101), HAL_EVENT_SHORT_PRESS);
}

/**
 * @brief 在窗口期内第二次按下但快速释放：应触发双击而非短按
 */
TEST(DoubleClickTest, SecondPressQuickReleaseReturnsDoubleClick)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次按下释放 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 50), HAL_EVENT_NONE);

    /* 窗口期内第二次按下（持续 200ms） */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 100), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 300), HAL_EVENT_DOUBLE_CLICK);
}

/**
 * @brief 连续两次双击：四个短按事件应正确识别为两次双击
 */
TEST(DoubleClickTest, TwoConsecutiveDoubleClicks)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次双击 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 50), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 100), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 150), HAL_EVENT_DOUBLE_CLICK);

    /* 第二次双击 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 500), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 550), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 600), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 650), HAL_EVENT_DOUBLE_CLICK);
}

/**
 * @brief 窗口超时的同一帧出现新按下：不应丢失按下事件
 * @note P1-1 回归：旧实现先检查超时并提前返回 SHORT_PRESS，导致本帧按下被丢弃。
 */
TEST(DoubleClickTest, PressAtWindowTimeoutDoesNotDropEvent)
{
    hal_input_dc_state_t st;
    hal_input_dc_init(&st);

    /* 第一次短按 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 0), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 100), HAL_EVENT_NONE);

    /* 窗口期刚好超时的同一帧出现第二次按下：
     * 应先返回上一次短按，并记录本次按下。 */
    EXPECT_EQ(hal_input_dc_process(&st, true, false, 401), HAL_EVENT_SHORT_PRESS);

    /* 本次按下释放后形成新的短按 */
    EXPECT_EQ(hal_input_dc_process(&st, false, true, 500), HAL_EVENT_NONE);
    EXPECT_EQ(hal_input_dc_process(&st, false, false, 801), HAL_EVENT_SHORT_PRESS);
}
