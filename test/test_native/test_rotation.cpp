#include <gtest/gtest.h>

/* 被测函数声明 —— 将在 src/app/rotation_helper.cpp 中实现 */
#ifdef __cplusplus
extern "C" {
#endif
int16_t resolve_rotation_level(uint8_t saved_rot);
#ifdef __cplusplus
}
#endif

/* Phase 1.1: 默认横屏 —— 旋转值解析测试 */

TEST(RotationTest, OldPortrait0DegMapsToNewPortrait) {
    EXPECT_EQ(resolve_rotation_level(0), 1);
}

TEST(RotationTest, OldLandscape90DegMapsToNewLandscape) {
    EXPECT_EQ(resolve_rotation_level(1), 2);
}

TEST(RotationTest, OldPortrait180DegMapsToNewPortrait) {
    EXPECT_EQ(resolve_rotation_level(2), 1);
}

TEST(RotationTest, OldLandscape270DegMapsToNewLandscape) {
    EXPECT_EQ(resolve_rotation_level(3), 2);
}

TEST(RotationTest, DefaultUnknownValueIsLandscape) {
    EXPECT_EQ(resolve_rotation_level(255), 2);
    EXPECT_EQ(resolve_rotation_level(4), 2);
}
