/**
 * @file   native_main.cpp
 * @brief  Native 测试环境主入口
 * @details 使用 GoogleTest 框架对 Xerintosh UI 核心进行单元测试。
 *          在 native 环境下运行，不依赖任何硬件库。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include "app/app_state.h"

#include "kernel/kern_task.h"

/* 桩回调（供 native 测试链接 app_init.c / app_menu.c 使用） */
void on_brightness_change_cb(void *ud) { (void)ud; }
void on_anim_speed_change_cb(void *ud) { (void)ud; }
void on_anim_enabled_change_cb(void *ud) { (void)ud; }
void on_screen_rotation_change_cb(void *ud) { (void)ud; }
void on_serial_baud_change_cb(void *ud) { (void)ud; }
void on_spring_mode_change_cb(void *ud) { (void)ud; }
void on_spring_stiffness_change_cb(void *ud) { (void)ud; }
void on_spring_damping_change_cb(void *ud) { (void)ud; }
}

/* ═══ 动画测试 ═══ */

/**
 * @brief 测试缓动动画是否收敛到目标值
 */
TEST(AnimationTest, EasingConverges)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos = 0.0f;
    float target = 100.0f;
    for (int i = 0; i < 200; i++) {
        xerintosh_animation(&pos, target, 92.0f);
    }
    EXPECT_FLOAT_EQ(pos, target);
}

/**
 * @brief 测试动画靠近目标时的吸附行为（diff <= threshold 直接跳转）
 */
TEST(AnimationTest, SnapsWhenClose)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos = 99.5f;
    float target = 100.0f;
    xerintosh_animation(&pos, target, 50.0f);
    EXPECT_FLOAT_EQ(pos, target) << "Should snap to target when within 1.0f";
}

/**
 * @brief 测试动画禁用时直接跳转到目标
 */
TEST(AnimationTest, InstantJumpWhenDisabled)
{
    xerintosh_context_init();
    g_anim_enabled = false;
    float pos = 0.0f;
    float target = 100.0f;
    xerintosh_animation(&pos, target, 50.0f);
    EXPECT_FLOAT_EQ(pos, target) << "Should instant-jump when disabled";
}

/**
 * @brief 测试 speed 超过上限时被裁剪到 ANIM_SPEED_MAX
 */
TEST(AnimationTest, SpeedClampedToMax)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos1 = 0.0f, pos2 = 0.0f;
    float target = 100.0f;
    /* speed=99.9 被裁剪到 99.0，行为应与 speed=99 一致（步进最大） */
    for (int i = 0; i < 20; i++) {
        xerintosh_animation(&pos1, target, 99.0f);
        xerintosh_animation(&pos2, target, 150.0f);  /* 应被裁剪到 99 */
    }
    EXPECT_FLOAT_EQ(pos1, pos2) << "Speed beyond max should be clamped";
}

/**
 * @brief 测试 speed 低于下限时被裁剪
 */
TEST(AnimationTest, SpeedClampedToMin)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos1 = 0.0f, pos2 = 0.0f;
    float target = 100.0f;
    for (int i = 0; i < 20; i++) {
        xerintosh_animation(&pos1, target, 0.0f);
        xerintosh_animation(&pos2, target, -50.0f);  /* 应被裁剪到 0 */
    }
    EXPECT_FLOAT_EQ(pos1, pos2) << "Negative speed should be clamped to zero";
}

/**
 * @brief 测试负方向移动
 */
TEST(AnimationTest, MovesTowardNegativeTarget)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos = 100.0f;
    float target = 0.0f;
    for (int i = 0; i < 80; i++) {
        xerintosh_animation(&pos, target, 92.0f);
    }
    EXPECT_LT(pos, 1.0f) << "Should move toward negative target";
}

/**
 * @brief 测试返回值：动画进行中返回 false
 */
TEST(AnimationTest, ReturnsFalseWhileAnimating)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos = 0.0f;
    bool settled = xerintosh_animation(&pos, 100.0f, 92.0f);
    EXPECT_FALSE(settled) << "Should return false while still animating";
}

/**
 * @brief 测试返回值：已稳定时返回 true
 */
TEST(AnimationTest, ReturnsTrueWhenSettled)
{
    xerintosh_context_init();
    g_anim_enabled = true;
    float pos = 50.0f;
    float target = 50.0f;
    bool settled = xerintosh_animation(&pos, target, 92.0f);
    EXPECT_TRUE(settled) << "Should return true when already at target";
}

/* ═══ 列表项测试 ═══ */

/**
 * @brief 测试根列表是否正确创建
 */
TEST(ItemTest, RootListCreated)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->type, list_item);
}

/**
 * @brief 测试子项挂载功能
 */
TEST(ItemTest, PushItem)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();
    xerintosh_list_item_t* item = xerintosh_new_list_item("Test", default_icon);
    bool result = xerintosh_push_item_to_list(root, item);
    EXPECT_TRUE(result);
}

/* ═══ 主入口 ═══ */

/**
 * @brief Native 测试主函数
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    hal_system_init();
    hal_display_init();
    hal_input_init();
    kern_sched_init();   /* 创建 idle 任务，为未显式 spawn 的测试提供当前任务上下文 */
    return RUN_ALL_TESTS();
}

#endif
