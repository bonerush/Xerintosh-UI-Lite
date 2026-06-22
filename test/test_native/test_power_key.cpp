/**
 * @file   test_power_key.cpp
 * @brief  电源键 HAL 层单元测试
 * @details 测试电源键状态机的事件检测逻辑：
 *          - 短按（< 1.5s 按下释放）
 *          - 长按（≥ 1.5s）
 *          - 持续按住（每 500ms 重复）
 *          - 按住时长计算
 */

#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_power_key.h"
}

/* ═══ 测试夹具 ═══ */

class PowerKeyTest : public ::testing::Test {
protected:
    void SetUp() override {
        hal_power_key_test_reset();
    }
};

/* ═══ 初始化测试 ═══ */

/**
 * @brief 初始化后状态为 NONE，未按下
 */
TEST_F(PowerKeyTest, InitStateIsNone)
{
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
    EXPECT_FALSE(hal_power_key_is_pressed());
    EXPECT_EQ(hal_power_key_get_hold_duration_ms(), 0u);
}

/* ═══ 短按测试 ═══ */

/**
 * @brief 短按（< 1.5s 按下释放）：释放时返回 SHORT_PRESS
 */
TEST_F(PowerKeyTest, ShortPressReturnsShortOnRelease)
{
    /* 按下 */
    hal_power_key_test_inject(true, 0);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
    EXPECT_TRUE(hal_power_key_is_pressed());

    /* 500ms 后释放 → 短按 */
    hal_power_key_test_inject(false, 500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_SHORT_PRESS);

    /* 之后返回 NONE，不再重复 */
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
    EXPECT_FALSE(hal_power_key_is_pressed());
}

/**
 * @brief 临界短按：刚好在 1499ms 释放仍为短按
 */
TEST_F(PowerKeyTest, ShortPressAtBoundary)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_get_event(); /* 消费 NONE */

    hal_power_key_test_inject(false, 1499);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_SHORT_PRESS);
}

/* ═══ 长按测试 ═══ */

/**
 * @brief 长按（≥ 1.5s）：达到阈值时返回 LONG_PRESS
 */
TEST_F(PowerKeyTest, LongPressReturnsLongAtThreshold)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_get_event(); /* 消费 NONE */

    /* 持续按下到 1500ms → 长按 */
    hal_power_key_test_inject(true, 1500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_LONG_PRESS);
    EXPECT_TRUE(hal_power_key_is_pressed());

    /* 释放后不产生额外事件 */
    hal_power_key_test_inject(false, 1600);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
    EXPECT_FALSE(hal_power_key_is_pressed());
}

/**
 * @brief 长按后释放：不应产生 SHORT_PRESS
 */
TEST_F(PowerKeyTest, LongPressReleaseNoShortPress)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_get_event();

    hal_power_key_test_inject(true, 1500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_LONG_PRESS);

    /* 释放 */
    hal_power_key_test_inject(false, 2000);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
}

/* ═══ 持续按住测试 ═══ */

/**
 * @brief 持续按住超过长按阈值后，每 500ms 触发一次 HOLD
 */
TEST_F(PowerKeyTest, HoldRepeatsAfterLongPress)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_get_event();

    /* 长按触发 */
    hal_power_key_test_inject(true, 1500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_LONG_PRESS);

    /* 又过了 500ms → HOLD */
    hal_power_key_test_inject(true, 2000);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_HOLD);

    /* 再过 500ms → 又一个 HOLD */
    hal_power_key_test_inject(true, 2500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_HOLD);
}

/**
 * @brief HOLD 不应在间隔不到 500ms 时触发
 */
TEST_F(PowerKeyTest, HoldDoesNotRepeatBeforeInterval)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_get_event();

    hal_power_key_test_inject(true, 1500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_LONG_PRESS);

    /* 仅过了 300ms，不应触发 HOLD */
    hal_power_key_test_inject(true, 1800);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);

    /* 再过 200ms（总共 500ms），触发 HOLD */
    hal_power_key_test_inject(true, 2000);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_HOLD);
}

/* ═══ 边界和回归测试 ═══ */

/**
 * @brief 连续两次短按：两次都应返回 SHORT_PRESS
 */
TEST_F(PowerKeyTest, TwoConsecutiveShortPresses)
{
    /* 第一次短按 */
    hal_power_key_test_inject(true, 0);
    hal_power_key_test_inject(false, 500);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_SHORT_PRESS);

    /* 第二次短按 */
    hal_power_key_test_inject(true, 1000);
    hal_power_key_test_inject(false, 1300);
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_SHORT_PRESS);
}

/**
 * @brief 事件消费：每次调用 get_event 只返回一个事件
 */
TEST_F(PowerKeyTest, EventConsumptionOnePerCall)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_test_inject(false, 100);

    /* 第一次调用返回 SHORT_PRESS */
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_SHORT_PRESS);

    /* 第二次调用返回 NONE（已消费） */
    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
}

/**
 * @brief reset 后状态干净
 */
TEST_F(PowerKeyTest, ResetClearsAllState)
{
    hal_power_key_test_inject(true, 0);
    hal_power_key_test_inject(true, 1500);
    hal_power_key_get_event(); /* LONG_PRESS */

    hal_power_key_test_reset();

    EXPECT_EQ(hal_power_key_get_event(), HAL_PWR_KEY_NONE);
    EXPECT_FALSE(hal_power_key_is_pressed());
}
