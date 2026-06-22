#include <gtest/gtest.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "app/flasher/flasher_gpio.h"
#ifdef __cplusplus
}
#endif

#include "app/flasher/flasher.h"

class FlasherGpioTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_flasher_pins[0] = {0,  FLASHER_SIG_BOOT, true};
        g_flasher_pins[1] = {26, FLASHER_SIG_TX,   true};
        g_flasher_pins[2] = {36, FLASHER_SIG_RX,   false};
    }
};

TEST_F(FlasherGpioTest, DefaultMapping) {
    EXPECT_EQ(g_flasher_pins[0].pin_num, 0);
    EXPECT_EQ(g_flasher_pins[0].role, FLASHER_SIG_BOOT);
    EXPECT_EQ(g_flasher_pins[1].role, FLASHER_SIG_TX);
    EXPECT_EQ(g_flasher_pins[2].role, FLASHER_SIG_RX);
}

TEST_F(FlasherGpioTest, GetPinForSignal) {
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_BOOT), 0);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_TX), 26);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_RX), 36);
}

TEST_F(FlasherGpioTest, SetPinRoleRejectsInvalid) {
    EXPECT_FALSE(flasher_set_pin_role(36, FLASHER_SIG_TX));
    EXPECT_TRUE(flasher_set_pin_role(0, FLASHER_SIG_BOOT));
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_BOOT), 0);
}

TEST_F(FlasherGpioTest, DuplicateRoleCleared) {
    /* G0 已默认占用 BOOT，把 G26 设为 BOOT 应自动解除 G0 */
    flasher_set_pin_role(26, FLASHER_SIG_BOOT);
    EXPECT_EQ(g_flasher_pins[0].role, FLASHER_SIG_NONE);
}

/* ═══ App Lifecycle Tests ═══ */

TEST(FlasherAppTest, LifecycleSmoke) {
    flasher_init(nullptr);
    flasher_loop(nullptr);
    flasher_exit(nullptr);
    SUCCEED();
}
