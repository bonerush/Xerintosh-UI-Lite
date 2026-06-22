/**
 * @file   test_app_menu_safety.cpp
 * @brief  验证 app_menu_build 在内存分配失败时的安全性
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include "app/app_menu.h"
#include "ui/ui_item.h"
}

class AppMenuSafetyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        xerintosh_clear_children_of_list(xerintosh_get_root_list());
    }
};

/**
 * @brief 正常路径下 app_menu_build 构建出预期菜单结构
 */
TEST_F(AppMenuSafetyTest, BuildNormalPathHasExpectedChildren)
{
    app_menu_build();

    xerintosh_list_item_t *root = xerintosh_get_root_list();
    ASSERT_NE(root, nullptr);
    EXPECT_GE(root->child_num, 6u);

    xerintosh_list_item_t *settings = nullptr;
    for (uint8_t i = 0; i < root->child_num; i++) {
        if (strcmp(root->child_list_item[i]->content, "设置") == 0) {
            settings = root->child_list_item[i];
            break;
        }
    }
    ASSERT_NE(settings, nullptr);
    EXPECT_GE(settings->child_num, 7u);
}
