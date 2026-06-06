#include <gtest/gtest.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "app/flasher/flasher_ui.h"
#ifdef __cplusplus
}
#endif

TEST(FlasherUiTest, MarqueeDotsCycle) {
    char buf[16];
    flasher_ui_build_marquee(buf, sizeof(buf), 0);
    EXPECT_STREQ(buf, "LOADING");
    flasher_ui_build_marquee(buf, sizeof(buf), 300);
    EXPECT_STREQ(buf, "LOADING.");
    flasher_ui_build_marquee(buf, sizeof(buf), 600);
    EXPECT_STREQ(buf, "LOADING..");
    flasher_ui_build_marquee(buf, sizeof(buf), 900);
    EXPECT_STREQ(buf, "LOADING...");
    flasher_ui_build_marquee(buf, sizeof(buf), 1200);
    EXPECT_STREQ(buf, "LOADING");
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
    EXPECT_EQ(st.status, FLASHER_UI_LOADING);
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
