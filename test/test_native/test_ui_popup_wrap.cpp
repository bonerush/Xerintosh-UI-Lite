/**
 * @file   test_ui_popup_wrap.cpp
 * @brief  弹窗换行缓冲区安全测试
 * @details 验证超长内容不会溢出 48 字节换行缓冲区。
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_context.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "hal/hal_system.h"
#include "hal/hal_display.h"
}

class UiPopupWrapTest : public ::testing::Test {
protected:
    void SetUp() override {
        xerintosh_context_init();
        hal_system_init();
        hal_display_init();
    }
};

TEST_F(UiPopupWrapTest, LongContentWrapDoesNotOverflow)
{
    char long_text[256];
    memset(long_text, 'A', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';

    EXPECT_NO_FATAL_FAILURE(xerintosh_push_pop_up(long_text, 1000));
    EXPECT_TRUE(g_xerintosh_pop_up.is_running);
}

TEST_F(UiPopupWrapTest, SingleLineFitsBuffer)
{
    EXPECT_NO_FATAL_FAILURE(xerintosh_push_pop_up("short", 1000));
    EXPECT_EQ(g_xerintosh_pop_up.wrap_line_count, 1u);
}
