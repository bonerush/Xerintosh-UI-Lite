#include <gtest/gtest.h>

/* 被测函数声明 */
#ifdef __cplusplus
extern "C" {
#endif
float astra_compute_scroll_offset(int16_t text_width, int16_t avail_width,
                                   bool is_selected, uint32_t elapsed_ms);
#ifdef __cplusplus
}
#endif

/* Phase 1.2: 文字滚动效果测试 */

TEST(MarqueeTest, ShortTextDoesNotScroll) {
    EXPECT_FLOAT_EQ(astra_compute_scroll_offset(30, 50, true, 0), 0.0f);
    EXPECT_FLOAT_EQ(astra_compute_scroll_offset(30, 50, true, 1000), 0.0f);
}

TEST(MarqueeTest, UnselectedItemDoesNotScroll) {
    EXPECT_FLOAT_EQ(astra_compute_scroll_offset(100, 50, false, 0), 0.0f);
    EXPECT_FLOAT_EQ(astra_compute_scroll_offset(100, 50, false, 1000), 0.0f);
}

TEST(MarqueeTest, SelectedLongTextStartsAtZero) {
    EXPECT_FLOAT_EQ(astra_compute_scroll_offset(100, 50, true, 0), 0.0f);
}

TEST(MarqueeTest, ScrollMovesLeftOverTime) {
    float offset_0ms  = astra_compute_scroll_offset(100, 50, true, 0);
    float offset_1s   = astra_compute_scroll_offset(100, 50, true, 1000);
    float offset_2s   = astra_compute_scroll_offset(100, 50, true, 2000);

    EXPECT_GT(offset_1s, offset_0ms);
    EXPECT_GT(offset_2s, offset_1s);
}

TEST(MarqueeTest, ScrollPausesAtEndBeforeLooping) {
    float offset_2s   = astra_compute_scroll_offset(100, 50, true, 2000);
    float offset_2_2s = astra_compute_scroll_offset(100, 50, true, 2200);
    float offset_2_4s = astra_compute_scroll_offset(100, 50, true, 2400);

    // During pause (2.0s - 2.5s), offset should stay the same
    EXPECT_FLOAT_EQ(offset_2s, offset_2_2s);
    EXPECT_FLOAT_EQ(offset_2_2s, offset_2_4s);
}

TEST(MarqueeTest, ScrollLoopsAfterPause) {
    float offset_0ms = astra_compute_scroll_offset(100, 50, true, 0);
    float offset_2_5s = astra_compute_scroll_offset(100, 50, true, 2500);

    // After one full cycle (2.0s scroll + 0.5s pause), back to start
    EXPECT_FLOAT_EQ(offset_0ms, offset_2_5s);
}

TEST(MarqueeTest, ScrollRespectsAvailableWidth) {
    // text_width=60, avail_width=50 => max_offset should be ~10
    float offset_2s = astra_compute_scroll_offset(60, 50, true, 2000);
    EXPECT_LE(offset_2s, 15.0f);  // 10 + small margin
    EXPECT_GT(offset_2s, 5.0f);
}
