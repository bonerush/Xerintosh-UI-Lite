#include <gtest/gtest.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "app/flasher/flasher_gpio.h"
#include "app/flasher/flasher_protocol.h"
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
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_DTR), 255);
}

TEST_F(FlasherGpioTest, SetPinRoleRejectsInvalid) {
    EXPECT_FALSE(flasher_set_pin_role(36, FLASHER_SIG_TX));
    EXPECT_TRUE(flasher_set_pin_role(0, FLASHER_SIG_DTR));
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_DTR), 0);
}

TEST_F(FlasherGpioTest, DuplicateRoleCleared) {
    flasher_set_pin_role(26, FLASHER_SIG_BOOT);
    EXPECT_EQ(g_flasher_pins[0].role, FLASHER_SIG_NONE);
}

/* ═══ Protocol Tests ═══ */

TEST(FlasherProtocolTest, SlipEncodeBasic) {
    uint8_t in[] = {0x01, 0x02, 0x03};
    uint8_t out[32];
    int len = flasher_slip_encode(in, 3, out, 32);
    EXPECT_EQ(len, 5);
    EXPECT_EQ(out[0], 0xC0);
    EXPECT_EQ(out[1], 0x01);
    EXPECT_EQ(out[2], 0x02);
    EXPECT_EQ(out[3], 0x03);
    EXPECT_EQ(out[4], 0xC0);
}

TEST(FlasherProtocolTest, SlipEncodeEscape) {
    uint8_t in[] = {0xC0, 0xDB, 0x00};
    uint8_t out[32];
    int len = flasher_slip_encode(in, 3, out, 32);
    EXPECT_EQ(len, 7); /* 3 data + 2 escape extra + 2 frame bytes */
    EXPECT_EQ(out[0], 0xC0);
    EXPECT_EQ(out[1], 0xDB);
    EXPECT_EQ(out[2], 0xDC); /* escaped 0xC0 */
    EXPECT_EQ(out[3], 0xDB);
    EXPECT_EQ(out[4], 0xDD); /* escaped 0xDB */
    EXPECT_EQ(out[5], 0x00);
    EXPECT_EQ(out[6], 0xC0);
}

TEST(FlasherProtocolTest, SlipDecodeRoundTrip) {
    uint8_t in[] = {0x01, 0xC0, 0xDB, 0x02};
    uint8_t slip[32], decoded[32];
    int enc_len = flasher_slip_encode(in, 4, slip, 32);
    int dec_len = flasher_slip_decode(slip, enc_len, decoded, 32);
    EXPECT_EQ(dec_len, 4);
    EXPECT_EQ(memcmp(decoded, in, 4), 0);
}

TEST(FlasherProtocolTest, SlipDecodeWithGarbagePrefix) {
    uint8_t slip[] = {0xAB, 0xCD, 0xC0, 0x01, 0x02, 0xC0};
    uint8_t decoded[32];
    int dec_len = flasher_slip_decode(slip, 6, decoded, 32);
    EXPECT_EQ(dec_len, 2);
    EXPECT_EQ(decoded[0], 0x01);
    EXPECT_EQ(decoded[1], 0x02);
}

TEST(FlasherProtocolTest, SlipDecodeNoFrame) {
    uint8_t garbage[] = {0x01, 0x02, 0x03};
    uint8_t decoded[32];
    int dec_len = flasher_slip_decode(garbage, 3, decoded, 32);
    EXPECT_EQ(dec_len, -1);
}

TEST(FlasherProtocolTest, BuildCmdSync) {
    uint8_t buf[64];
    uint8_t sync_data[36] = {0x07, 0x07, 0x12, 0x20};
    memset(sync_data + 4, 0x55, 32);
    int len = flasher_build_cmd(buf, 64, FLASHER_CMD_SYNC, 0, sync_data, 36);
    EXPECT_EQ(len, 44);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x08);
    EXPECT_EQ(buf[2], 36);
    EXPECT_EQ(buf[3], 0);
}

TEST(FlasherProtocolTest, BuildCmdNoData) {
    uint8_t buf[16];
    int len = flasher_build_cmd(buf, 16, FLASHER_CMD_FLASH_END, 0x12345678, NULL, 0);
    EXPECT_EQ(len, 8);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0xD4);
    EXPECT_EQ(buf[2], 0);
    EXPECT_EQ(buf[3], 0);
    EXPECT_EQ(buf[4], 0x78); /* checksum LSB */
    EXPECT_EQ(buf[5], 0x56);
    EXPECT_EQ(buf[6], 0x34);
    EXPECT_EQ(buf[7], 0x12);
}

TEST(FlasherProtocolTest, ProgressCalc) {
    flasher_session_t s;
    flasher_session_init(&s, 0x10000, 1024);
    s.written_size = 256;
    EXPECT_EQ(flasher_get_progress(&s), 25);
    s.written_size = 1024;
    EXPECT_EQ(flasher_get_progress(&s), 100);
}

TEST(FlasherProtocolTest, ProgressZeroTotal) {
    flasher_session_t s;
    flasher_session_init(&s, 0x10000, 0);
    EXPECT_EQ(flasher_get_progress(&s), 0);
}

TEST(FlasherProtocolTest, ProgressClamped) {
    flasher_session_t s;
    flasher_session_init(&s, 0x10000, 100);
    s.written_size = 200;
    EXPECT_EQ(flasher_get_progress(&s), 100);
}

/* ═══ App Lifecycle Tests ═══ */

TEST(FlasherAppTest, LifecycleSmoke) {
    flasher_init(nullptr);
    flasher_loop(nullptr);
    flasher_exit(nullptr);
    SUCCEED();
}
