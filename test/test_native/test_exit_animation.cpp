#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_context.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "hal/hal_system.h"
#include "hal/hal_display.h"
}

/* 简单的 stub 函数，供 user_item 使用 */
extern "C" void stub_user_init(void *ud) { (void)ud; }
extern "C" void stub_user_loop(void *ud) { (void)ud; }
extern "C" void stub_user_exit(void *ud) { (void)ud; }

/* ═══ Phase 1: 退场动画必须能在有限帧内完成 ═══ */

/**
 * @brief 退场动画不应卡死：在有限帧内必须完成并标记 finished
 * @note  这是 RED 测试：当前实现使用 float 精确相等比较，
 *        多次进入/退出后可能因浮点精度问题永远卡死。
 */
TEST(ExitAnimationTest, AnimationCompletesInFiniteFrames)
{
    /* 初始化 UI 核心 */
    xerintosh_context_init();
    g_in_xerintosh = true;
    g_xerintosh_exit_animation_finished = true;
    g_xerintosh_exit_animation_status = 0;
    hal_system_init();
    hal_display_init();
    xerintosh_init_core();

    /* 手动创建一个 user_item 并绑定到选择器，避免 app_init_ui 的外部依赖 */
    xerintosh_list_item_t* user_item_base = xerintosh_new_user_item(
        "TestApp", stub_user_init, stub_user_loop, stub_user_exit, user_icon);
    ASSERT_NE(user_item_base, nullptr);

    xerintosh_list_item_t* root = xerintosh_get_root_list();
    xerintosh_push_item_to_list(root, user_item_base);

    /* 将选择器绑定到这个 user_item */
    xerintosh_bind_item_to_selector(user_item_base);

    xerintosh_user_item_t* user = xerintosh_to_user_item(user_item_base);
    ASSERT_NE(user, nullptr);

    /* 模拟进入 user_item */
    user->entering_user_item = true;
    user->exiting_user_item = false;
    user->in_user_item = false;
    g_xerintosh_exit_animation_finished = false;

    /* 运行多帧，让进入动画完成 */
    for (int i = 0; i < 500; i++) {
        xerintosh_ui_main_core();
        if (user->in_user_item) break;
    }
    EXPECT_TRUE(user->in_user_item)
        << "Failed to enter user_item";

    /* 现在模拟退出 */
    user->entering_user_item = false;
    user->exiting_user_item = true;
    user->in_user_item = true;
    g_xerintosh_exit_animation_finished = false;

    /* 运行退出动画，最多 500 帧 */
    int frames = 0;
    for (; frames < 500; frames++) {
        xerintosh_ui_main_core();
        if (g_xerintosh_exit_animation_finished) {
            break;
        }
    }

    /* 断言：动画必须在 500 帧内完成 */
    EXPECT_TRUE(g_xerintosh_exit_animation_finished)
        << "Exit animation did not complete after 500 frames (status="
        << (int)g_xerintosh_exit_animation_status << ")";
    EXPECT_LT(frames, 500)
        << "Exit animation took too many frames: " << frames;
}

/**
 * @brief 多次进入/退出 user_item 后，动画状态机仍应正常工作
 */
TEST(ExitAnimationTest, MultipleEnterExitCycles)
{
    xerintosh_context_init();
    g_in_xerintosh = true;
    hal_system_init();
    hal_display_init();
    xerintosh_init_core();

    xerintosh_list_item_t* user_item_base = xerintosh_new_user_item(
        "TestApp", stub_user_init, stub_user_loop, stub_user_exit, user_icon);
    ASSERT_NE(user_item_base, nullptr);

    xerintosh_list_item_t* root = xerintosh_get_root_list();
    xerintosh_push_item_to_list(root, user_item_base);
    xerintosh_bind_item_to_selector(user_item_base);

    xerintosh_user_item_t* user = xerintosh_to_user_item(user_item_base);
    ASSERT_NE(user, nullptr);

    /* 循环 3 次进入/退出 */
    for (int cycle = 0; cycle < 3; cycle++) {
        /* 进入 */
        user->entering_user_item = true;
        user->exiting_user_item = false;
        user->in_user_item = false;
        g_xerintosh_exit_animation_finished = false;
        g_xerintosh_exit_animation_status = 0;

        int enter_frames = 0;
        for (; enter_frames < 500; enter_frames++) {
            xerintosh_ui_main_core();
            if (user->in_user_item) break;
        }
        EXPECT_TRUE(user->in_user_item)
            << "Cycle " << cycle << ": Failed to enter user_item";

        /* 退出 */
        user->entering_user_item = false;
        user->exiting_user_item = true;
        user->in_user_item = true;
        g_xerintosh_exit_animation_finished = false;
        /* 注意：handle_user_item_exit 不会重置 status，
           由动画函数的 _last_finished 检测自动重置 */

        int exit_frames = 0;
        for (; exit_frames < 500; exit_frames++) {
            xerintosh_ui_main_core();
            if (g_xerintosh_exit_animation_finished) break;
        }
        EXPECT_TRUE(g_xerintosh_exit_animation_finished)
            << "Cycle " << cycle << ": Exit animation stuck (status="
            << (int)g_xerintosh_exit_animation_status << ")";
    }
}
