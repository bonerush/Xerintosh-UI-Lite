#include <gtest/gtest.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "app/flasher/flasher_ui.h"
#ifdef __cplusplus
}
#endif

TEST(FlasherUiTest, MarqueeDotsCycleBridge) {
    char buf[16];
    flasher_ui_build_marquee(buf, sizeof(buf), 0, true);
    EXPECT_STREQ(buf, "BRIDGE");
    flasher_ui_build_marquee(buf, sizeof(buf), 300, true);
    EXPECT_STREQ(buf, "BRIDGE.");
    flasher_ui_build_marquee(buf, sizeof(buf), 600, true);
    EXPECT_STREQ(buf, "BRIDGE..");
    flasher_ui_build_marquee(buf, sizeof(buf), 900, true);
    EXPECT_STREQ(buf, "BRIDGE...");
    flasher_ui_build_marquee(buf, sizeof(buf), 1200, true);
    EXPECT_STREQ(buf, "BRIDGE");
}

TEST(FlasherUiTest, MarqueeDotsCycleFlashing) {
    char buf[16];
    flasher_ui_build_marquee(buf, sizeof(buf), 0, false);
    EXPECT_STREQ(buf, "FLASHING");
    flasher_ui_build_marquee(buf, sizeof(buf), 300, false);
    EXPECT_STREQ(buf, "FLASHING.");
    flasher_ui_build_marquee(buf, sizeof(buf), 600, false);
    EXPECT_STREQ(buf, "FLASHING..");
    flasher_ui_build_marquee(buf, sizeof(buf), 900, false);
    EXPECT_STREQ(buf, "FLASHING...");
    flasher_ui_build_marquee(buf, sizeof(buf), 1200, false);
    EXPECT_STREQ(buf, "FLASHING");
}

TEST(FlasherUiTest, ProgressClamping) {
    flasher_ui_state_t st;
    flasher_ui_init(&st);
    flasher_ui_set_progress(&st, -5);
    EXPECT_EQ(st.progress, 0);
    flasher_ui_set_progress(&st, 150);
    EXPECT_EQ(st.progress, 100);
}

TEST(FlasherUiTest, StatusTransitions) {
    flasher_ui_state_t st;
    flasher_ui_init(&st);
    EXPECT_EQ(st.status, FLASHER_UI_BRIDGE);
    flasher_ui_set_status(&st, FLASHER_UI_SUCCESS);
    EXPECT_EQ(st.status, FLASHER_UI_SUCCESS);
}

TEST(FlasherUiTest, RenderFrameSmoke) {
    /* Smoke test: should not crash */
    flasher_ui_state_t st;
    flasher_ui_init(&st);
    flasher_ui_set_progress(&st, 50);
    flasher_ui_draw(&st);
    SUCCEED();
}
