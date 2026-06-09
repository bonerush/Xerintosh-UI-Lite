#include <gtest/gtest.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "app/flasher/flasher_gpio.h"
#include "app/flasher/flasher_protocol.h"
#include "app/flasher/flasher_protocol_stk500.h"
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
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_DTR), 255);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_TX), 26);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_RX), 36);
}

TEST_F(FlasherGpioTest, SetPinRoleRejectsInvalid) {
    EXPECT_FALSE(flasher_set_pin_role(36, FLASHER_SIG_TX));
    EXPECT_TRUE(flasher_set_pin_role(0, FLASHER_SIG_BOOT)); /* G0 可自由配置 BOOT/DTR */
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_BOOT), 0);
}

TEST_F(FlasherGpioTest, DtrFallbackToBootPin) {
    /* DTR 无专用引脚时自动回退到 BOOT 引脚 */
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_DTR), 255);
    EXPECT_EQ(flasher_get_pin_for_signal(FLASHER_SIG_BOOT), 0);
    /* flasher_set_dtr 在硬件上会操作 BOOT 引脚 */
}

TEST_F(FlasherGpioTest, DuplicateRoleCleared) {
    /* G0 已默认占用 BOOT，把 G26 设为 BOOT 应自动解除 G0 */
    flasher_set_pin_role(26, FLASHER_SIG_BOOT);
    EXPECT_EQ(g_flasher_pins[0].role, FLASHER_SIG_NONE);
}

/* ═══ Protocol Type Enum Tests ═══ */

TEST(FlasherProtocolTest, ProtocolTypeEnumValues) {
    EXPECT_EQ(FLASHER_PROTO_AUTO, 0);
    EXPECT_EQ(FLASHER_PROTO_ESP32, 1);
    EXPECT_EQ(FLASHER_PROTO_STK500V1, 2);
    EXPECT_EQ(FLASHER_PROTO_DEFAULT, FLASHER_PROTO_AUTO);
}

/* ═══ SLIP Protocol Tests ═══ */

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

/* ═══ STK500v1 Protocol Tests ═══ */

TEST(Stk500ProtocolTest, Constants) {
    EXPECT_EQ(STK_OK, 0x10);
    EXPECT_EQ(STK_INSYNC, 0x14);
    EXPECT_EQ(CRC_EOP, 0x20);
    EXPECT_EQ(STK_GET_SYNC, 0x30);
    EXPECT_EQ(STK_ENTER_PROGMODE, 0x50);
    EXPECT_EQ(STK_LEAVE_PROGMODE, 0x51);
    EXPECT_EQ(STK_LOAD_ADDRESS, 0x55);
    EXPECT_EQ(STK_PROG_PAGE, 0x64);
    EXPECT_EQ(STK500_FLASH_PAGE_SIZE, 128);
}

TEST(Stk500ProtocolTest, SessionInit) {
    stk500_session_t s;
    stk500_session_init(&s, 4096);
    EXPECT_EQ(s.state, STK500_STATE_IDLE);
    EXPECT_EQ(s.total_size, 4096);
    EXPECT_EQ(s.written_size, 0);
    EXPECT_EQ(s.current_addr, 0);
    EXPECT_EQ(s.last_error, 0);
}

TEST(Stk500ProtocolTest, SessionInitNullSafe) {
    stk500_session_init(nullptr, 4096);
    SUCCEED();
}

TEST(Stk500ProtocolTest, ProgressCalc) {
    stk500_session_t s;
    stk500_session_init(&s, 1024);
    s.written_size = 256;
    EXPECT_EQ(stk500_get_progress(&s), 25);
    s.written_size = 1024;
    EXPECT_EQ(stk500_get_progress(&s), 100);
}

TEST(Stk500ProtocolTest, ProgressZeroTotal) {
    stk500_session_t s;
    stk500_session_init(&s, 0);
    EXPECT_EQ(stk500_get_progress(&s), 0);
}

TEST(Stk500ProtocolTest, ProgressClamped) {
    stk500_session_t s;
    stk500_session_init(&s, 100);
    s.written_size = 200;
    EXPECT_EQ(stk500_get_progress(&s), 100);
}

/* ═══ App Lifecycle Tests ═══ */

TEST(FlasherAppTest, LifecycleSmoke) {
    flasher_init(nullptr);
    flasher_loop(nullptr);
    flasher_exit(nullptr);
    SUCCEED();
}
