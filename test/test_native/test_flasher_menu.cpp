/**
 * @file   test_flasher_menu.cpp
 * @brief  烧录器菜单单元测试
 * @details 验证角色标签映射、引脚查找等可在 native 环境独立测试的逻辑。
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/flasher/flasher_gpio.h"
}

class FlasherMenuTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* 重置为硬件默认映射，避免 storage stub 中残留数据影响断言 */
        g_flasher_pins[0] = {0,  FLASHER_SIG_BOOT, true};
        g_flasher_pins[1] = {26, FLASHER_SIG_TX,   true};
        g_flasher_pins[2] = {36, FLASHER_SIG_RX,   false};
    }
};

TEST_F(FlasherMenuTest, RoleLabelMapsRoles)
{
    EXPECT_STREQ(flasher_role_label(FLASHER_SIG_NONE), "未分配");
    EXPECT_STREQ(flasher_role_label(FLASHER_SIG_TX), "TX");
    EXPECT_STREQ(flasher_role_label(FLASHER_SIG_RX), "RX");
    EXPECT_STREQ(flasher_role_label(FLASHER_SIG_BOOT), "BOOT/DTR");
}

TEST_F(FlasherMenuTest, RoleLabelUnknownReturnsPlaceholder)
{
    EXPECT_STREQ(flasher_role_label((flasher_signal_t)99), "?");
}

TEST_F(FlasherMenuTest, DefaultPinConfigHasValidRoles)
{
    /* 默认加载后，至少 BOOT 和 TX 应被分配到 G0/G26 */
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_BOOT), 0);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_TX), 26);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_RX), 36);
}
