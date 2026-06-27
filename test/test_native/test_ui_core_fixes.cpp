/**
 * @file   test_ui_core_fixes.cpp
 * @brief  UI 核心层回归测试（第十一轮修复）
 * @details 覆盖 hide_pop_up 脏标志、slider INT16_MIN 绘制、无效类型派发、
 *          选择器安全移出、列表项移除防悬垂指针等边界场景。
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_context.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_dirty.h"
#include "ui/ui_drawer.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
}

class UiCoreFixesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        xerintosh_context_init();
        hal_system_init();
        hal_display_init();
        xerintosh_clear_children_of_list(xerintosh_get_root_list());
    }

    void TearDown() override
    {
        xerintosh_clear_children_of_list(xerintosh_get_root_list());
    }
};

/* ═══ P1-7: hide_pop_up 必须标记脏矩形 ═══ */
TEST_F(UiCoreFixesTest, HidePopUpInvalidates)
{
    xerintosh_push_pop_up("hello", 1000);
    xerintosh_clear_dirty();
    ASSERT_FALSE(xerintosh_is_dirty());

    xerintosh_hide_pop_up();
    EXPECT_TRUE(xerintosh_is_dirty());
}

/* ═══ P2-7: slider 值 INT16_MIN 绘制不溢出/崩溃 ═══ */
TEST_F(UiCoreFixesTest, SliderInt16MinDrawDoesNotCrash)
{
    static int16_t s_value = INT16_MIN;
    xerintosh_list_item_t *slider = xerintosh_new_slider_item(
        "min", &s_value, 1, INT16_MIN, INT16_MAX, NULL, NULL, slider_icon);
    ASSERT_NE(slider, nullptr);

    /* 进入编辑模式才会显示数值覆盖层 */
    xerintosh_dispatch_enter(slider);
    EXPECT_TRUE(xerintosh_to_slider_item(slider)->is_confirmed);

    /* 绘制本体和覆盖层，itoa 不应因 -INT16_MIN 溢出而崩溃 */
    xerintosh_dispatch_draw(slider, 10, 20);
    xerintosh_dispatch_draw_overlay(slider);
    EXPECT_EQ(s_value, INT16_MIN);

    free((void *)slider->content);
    free(slider);
}

/* ═══ P2-8: 无效 type 值不应导致数组越界 ═══ */
TEST_F(UiCoreFixesTest, DispatchWithInvalidTypeIsNoOp)
{
    xerintosh_list_item_t *item = xerintosh_new_list_item("list", list_icon);
    ASSERT_NE(item, nullptr);

    /* 模拟内存损坏或枚举越界 */
    item->type = (xerintosh_list_item_type_t)99;

    EXPECT_NO_FATAL_FAILURE(xerintosh_dispatch_enter(item));
    EXPECT_EQ(xerintosh_dispatch_measure(item), 0);
    EXPECT_NO_FATAL_FAILURE(xerintosh_dispatch_draw(item, 0, 0));
    EXPECT_NO_FATAL_FAILURE(xerintosh_dispatch_destroy(item));

    free((void *)item->content);
    free(item);
}

/* ═══ 选择器安全移出：移除子树时避免悬垂指针 ═══ */
TEST_F(UiCoreFixesTest, SelectorSafetyMoveOutFromSubtree)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item("A", list_icon);
    xerintosh_list_item_t *a1 = xerintosh_new_list_item("A1", list_icon);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(a1, nullptr);

    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));
    EXPECT_TRUE(xerintosh_push_item_to_list(a, a1));

    /* 选中子树 A 内部的 A1 */
    g_xerintosh_selector.selected_item = a1;
    g_xerintosh_selector.selected_index = 0;

    /* 将 A 标记为待移除，root 是 fallback_parent；root 只有 A 一个子项 */
    ui_selector_safety_move_out(a, root);

    /* 选择器应被提升到 root，而不是继续指向 A/A1 */
    EXPECT_EQ(g_xerintosh_selector.selected_item, root);

    xerintosh_clear_children_of_list(root);
}

/* ═══ P3-5: 移除子项前自动安全移出选择器 ═══ */
TEST_F(UiCoreFixesTest, RemoveItemMovesSelectorSafely)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item("A", list_icon);
    xerintosh_list_item_t *b = xerintosh_new_list_item("B", list_icon);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));
    EXPECT_TRUE(xerintosh_push_item_to_list(root, b));

    /* 选中即将被移除的 A */
    g_xerintosh_selector.selected_item = a;
    g_xerintosh_selector.selected_index = 0;

    EXPECT_TRUE(xerintosh_remove_item_from_list(root, a));

    /* 移除后选择器不应指向已释放的 A，而应落到安全的 B */
    EXPECT_EQ(g_xerintosh_selector.selected_item, b);
    EXPECT_EQ(g_xerintosh_selector.selected_index, 0u);

    xerintosh_clear_children_of_list(root);
}

/* ═══ P2-9: 祖父节点缺失时退出不应崩溃 ═══ */
TEST_F(UiCoreFixesTest, ExitCurrentItemGuardsNullGrandparent)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item("A", list_icon);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));

    xerintosh_init_core();
    g_in_xerintosh = true;

    ASSERT_EQ(g_xerintosh_selector.selected_item, a);
    xerintosh_selector_exit_current_item();

    /* 主菜单层(layer 0)不允许退出，选择器应保持不变 */
    EXPECT_EQ(g_xerintosh_selector.selected_item, a);

    xerintosh_clear_children_of_list(root);
}

/* ═══ P1-3 / P1-4: 未注册双键回调时 UI 主循环仍可运行 ═══ */
TEST_F(UiCoreFixesTest, MainLoopWithoutDualKeyCallbackDoesNotCrash)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item("A", list_icon);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));

    g_in_xerintosh = true;
    for (int i = 0; i < 5; i++) {
        xerintosh_ui_main_core();
    }

    xerintosh_clear_children_of_list(root);
}

/* ═══ U4: 绘制入口对空 parent / 空列表的保护 ═══ */
TEST_F(UiCoreFixesTest, DrawListItemGuardsNullParent)
{
    xerintosh_context_init();
    g_in_xerintosh = true;

    /* 构造 selected_item 存在但 parent 为 NULL 的异常状态 */
    xerintosh_list_item_t orphan = {};
    orphan.content = "orphan";
    g_xerintosh_selector.selected_item = &orphan;
    g_xerintosh_selector.selected_index = 0;

    EXPECT_NO_FATAL_FAILURE(xerintosh_draw_list());
}

TEST_F(UiCoreFixesTest, DrawListItemGuardsEmptyChildren)
{
    xerintosh_context_init();
    g_in_xerintosh = true;

    xerintosh_list_item_t root = {};
    root.child_num = 0;

    g_xerintosh_selector.selected_item = &root;
    g_xerintosh_selector.selected_index = 0;

    EXPECT_NO_FATAL_FAILURE(xerintosh_draw_list());
}

TEST_F(UiCoreFixesTest, InitCoreHandlesEmptyRoot)
{
    xerintosh_context_init();
    xerintosh_clear_children_of_list(xerintosh_get_root_list());
    g_in_xerintosh = true;

    EXPECT_NO_FATAL_FAILURE(xerintosh_init_core());
    EXPECT_EQ(g_xerintosh_selector.selected_item, nullptr);
}

/* ═══ U7: 选择器绑定后速度复位 ═══ */
TEST_F(UiCoreFixesTest, SelectorVelocitiesResetAfterBind)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    xerintosh_list_item_t *a = xerintosh_new_list_item("A", list_icon);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(xerintosh_push_item_to_list(root, a));

    g_xerintosh_selector.v_y_selector = 1.0f;
    g_xerintosh_selector.v_w_selector = 2.0f;
    g_xerintosh_selector.v_h_selector = 3.0f;

    xerintosh_bind_item_to_selector(a);

    EXPECT_FLOAT_EQ(g_xerintosh_selector.v_y_selector, 0.0f);
    EXPECT_FLOAT_EQ(g_xerintosh_selector.v_w_selector, 0.0f);
    EXPECT_FLOAT_EQ(g_xerintosh_selector.v_h_selector, 0.0f);

    xerintosh_clear_children_of_list(root);
}
