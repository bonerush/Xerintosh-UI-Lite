/**
 * @file   test_spring_anim.cpp
 * @brief  xerintosh_spring_animation 弹簧动画单元测试
 */

#include <gtest/gtest.h>
#include <math.h>

extern "C" {
#include "ui/ui_core.h"
#include "ui/ui_context.h"
}

/* ═══ 基础设施 ═══ */

class SpringAnimTest : public ::testing::Test {
protected:
    void SetUp() override {
        xerintosh_context_init();
        g_anim_enabled = true;
    }
};

/* ═══ 阶跃响应测试（step response） ═══ */

TEST_F(SpringAnimTest, MovesTowardTargetFromRest)
{
    float pos = 0.0f;
    float vel = 0.0f;
    float target = 100.0f;

    xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);

    /* 从静止开始，应向目标方向移动 */
    EXPECT_GT(pos, 0.0f) << "Should move toward positive target";
    EXPECT_GT(vel, 0.0f) << "Velocity should be positive toward target";
}

TEST_F(SpringAnimTest, MovesTowardNegativeTarget)
{
    float pos = 100.0f;
    float vel = 0.0f;
    float target = 0.0f;

    xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);

    /* 应向负方向移动 */
    EXPECT_LT(pos, 100.0f) << "Should move toward negative target";
    EXPECT_LT(vel, 0.0f) << "Velocity should be negative toward target";
}

TEST_F(SpringAnimTest, OvershootsWithLowDamping)
{
    float pos = 0.0f;
    float vel = 0.0f;
    float target = 100.0f;

    /* damping=0.05 (极低阻尼) → 严重欠阻尼，明显超调 */
    for (int i = 0; i < 40; i++) {
        xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.05f);
    }

    /* 经过足够帧数，应已明显超过目标 */
    EXPECT_GT(pos, target + 1.0f) << "Underdamped spring should overshoot target";
}

TEST_F(SpringAnimTest, NoOvershootWithHighDamping)
{
    float pos = 0.0f;
    float vel = 0.0f;
    float target = 100.0f;

    /* damping=0.80 (高阻尼) → 过阻尼/临界阻尼，无超调 */
    for (int i = 0; i < 80; i++) {
        xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.80f);
    }

    EXPECT_LE(pos, target + 0.5f)
        << "Overdamped spring should not overshoot";
    EXPECT_NEAR(pos, target, 0.5f)
        << "Overdamped spring should converge to target";
}

/* ═══ 收敛测试（convergence） ═══ */

TEST_F(SpringAnimTest, ConvergesToTarget)
{
    float pos = 0.0f;
    float vel = 0.0f;
    float target = 50.0f;

    for (int i = 0; i < 100; i++) {
        xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);
    }

    EXPECT_NEAR(pos, target, 0.5f) << "Should converge to target";
    EXPECT_NEAR(vel, 0.0f, 0.5f) << "Velocity should settle to zero";
}

TEST_F(SpringAnimTest, HigherStiffnessRespondsFaster)
{
    /* 用高刚度：5帧后的位置 */
    float pos_fast = 0.0f;
    float vel_fast = 0.0f;
    float target = 100.0f;
    for (int i = 0; i < 5; i++) {
        xerintosh_spring_animation(&pos_fast, &vel_fast, target, 0.20f, 0.30f);
    }

    /* 用低刚度：5帧后的位置 */
    float pos_slow = 0.0f;
    float vel_slow = 0.0f;
    for (int i = 0; i < 5; i++) {
        xerintosh_spring_animation(&pos_slow, &vel_slow, target, 0.05f, 0.30f);
    }

    /* 高刚度应在相同帧数内移动更远 */
    EXPECT_GT(pos_fast, pos_slow)
        << "Higher stiffness should move farther in same frames";
}

/* ═══ 吸附测试（snapping） ═══ */

TEST_F(SpringAnimTest, SnapsWhenCloseAndSlow)
{
    float pos = 49.6f;   /* 距目标 < 0.5 */
    float vel = 0.1f;     /* 速度 < 0.5 */
    float target = 50.0f;

    xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);

    /* 应吸附到目标并清零速度 */
    EXPECT_FLOAT_EQ(pos, target) << "Should snap to exact target";
    EXPECT_FLOAT_EQ(vel, 0.0f) << "Velocity should be zeroed";
}

TEST_F(SpringAnimTest, DoesNotSnapWhenVelocityHigh)
{
    float pos = 49.6f;
    float vel = 5.0f;    /* 速度 > 0.5 */
    float target = 50.0f;

    xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);

    /* 位置接近但速度大，不应吸附 */
    EXPECT_NE(pos, target) << "Should NOT snap when velocity is high";
    EXPECT_NE(vel, 0.0f) << "Velocity should NOT be zeroed";
}

/* ═══ 动画禁用测试 ═══ */

TEST_F(SpringAnimTest, InstantSnapWhenAnimDisabled)
{
    float pos = 0.0f;
    float vel = 10.0f;
    float target = 100.0f;

    g_anim_enabled = false;
    xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);

    /* 动画禁用时应瞬间跳转 */
    EXPECT_FLOAT_EQ(pos, target) << "Should instantly jump to target";
    EXPECT_FLOAT_EQ(vel, 0.0f) << "Velocity should be zeroed";
}

/* ═══ 稳定状态测试 ═══ */

TEST_F(SpringAnimTest, NoChangeWhenAlreadyAtTarget)
{
    float pos = 50.0f;
    float vel = 0.0f;
    float target = 50.0f;

    /* 已在目标且速度为零：force=0，不应移动 */
    for (int i = 0; i < 5; i++) {
        xerintosh_spring_animation(&pos, &vel, target, 0.10f, 0.30f);
    }

    EXPECT_FLOAT_EQ(pos, target) << "Should stay at target";
}

/* ═══ 对称性测试 ═══ */

TEST_F(SpringAnimTest, SymmetricResponse)
{
    float pos_up = 0.0f, vel_up = 0.0f;
    float pos_down = 100.0f, vel_down = 0.0f;

    /* 足够多帧让两个方向都收敛 */
    for (int i = 0; i < 80; i++) {
        xerintosh_spring_animation(&pos_up, &vel_up, 50.0f, 0.10f, 0.30f);
        xerintosh_spring_animation(&pos_down, &vel_down, 50.0f, 0.10f, 0.30f);
    }

    EXPECT_NEAR(pos_up, 50.0f, 0.5f);
    EXPECT_NEAR(pos_down, 50.0f, 0.5f);
}
