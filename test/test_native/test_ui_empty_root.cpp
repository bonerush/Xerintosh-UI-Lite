/**
 * @file   test_ui_empty_root.cpp
 * @brief  验证空根节点时 UI 核心不会崩溃
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_context.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
}

/**
 * @brief 空根节点下调用 UI 核心初始化与主循环不应崩溃
 * @note  之前框架要求根节点至少有一个子项，否则内部会越界读取。
 *        本测试覆盖“未添加任何菜单项”的边界场景。
 */
TEST(UiEmptyRootTest, CoreMainLoopHandlesEmptyRoot)
{
    xerintosh_context_init();
    xerintosh_clear_children_of_list(xerintosh_get_root_list());
    g_in_xerintosh = true;

    xerintosh_init_core();

    /* 空根节点下选择器应为空 */
    EXPECT_EQ(g_xerintosh_selector.selected_item, nullptr);

    /* 主循环多帧不应崩溃 */
    for (int i = 0; i < 10; i++) {
        xerintosh_ui_main_core();
    }
}

/**
 * @brief 空根节点下选择器导航 API 不应崩溃
 */
TEST(UiEmptyRootTest, SelectorNavigationHandlesEmptyRoot)
{
    xerintosh_context_init();
    xerintosh_clear_children_of_list(xerintosh_get_root_list());
    g_in_xerintosh = true;

    xerintosh_init_core();

    xerintosh_selector_go_next_item();
    xerintosh_selector_go_prev_item();
    xerintosh_selector_jump_to_selected_item();
    xerintosh_selector_exit_current_item();

    EXPECT_EQ(g_xerintosh_selector.selected_item, nullptr);
}
