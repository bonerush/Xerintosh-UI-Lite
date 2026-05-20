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

/* Phase 1.2: 文字循环滚动效果测试 */

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

TEST(MarqueeTest, ScrollLoopsAfterFullCycle) {
    float offset_0ms = astra_compute_scroll_offset(100, 50, true, 0);
    float offset_3s  = astra_compute_scroll_offset(100, 50, true, 3000);

    // 一个完整 3s 周期后，offset 应回到起点
    EXPECT_NEAR(offset_0ms, offset_3s, 0.1f);
}

TEST(MarqueeTest, ScrollRespectsCycleDistance) {
    // text_width=60, avail_width=50 => cycle_distance=110
    // 1.5s 时 (半周期)，offset 应约为 55
    float offset_1_5s = astra_compute_scroll_offset(60, 50, true, 1500);
    EXPECT_NEAR(offset_1_5s, 55.0f, 2.0f);
}
