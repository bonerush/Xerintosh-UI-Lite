/**
 * @file   test_tu_app.cpp
 * @brief  Token Usage App 生命周期单元测试
 * @details 验证 token_usage_loop 在 API key 为空时跳过网络请求，
 *          以及 token_usage_get_data() 暴露内部状态供测试断言。
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/token_usage/tu_app.h"
#include "stubs/hal_stubs.h"
}

TEST(TuAppTest, Loop_WithEmptyKey_SkipsFetch)
{
    fake_input_reset();
    fake_system_set_ticks(0);

    token_usage_init(NULL);
    EXPECT_FALSE(token_usage_get_data()->deepseek_ok);

    token_usage_loop(NULL);

    EXPECT_FALSE(token_usage_get_data()->deepseek_ok)
        << "Empty API key should skip fetch and leave deepseek_ok false";
}

TEST(TuAppTest, Loop_WithValidKey_FetchesData)
{
    /* NATIVE_TEST 环境下 storage.cpp 的桩固定返回空 key，无法在不修改
     * storage 模块的前提下注入有效 key。此测试占位，待未来引入 storage
     * 测试钩子后再启用。 */
    GTEST_SKIP() << "storage stub cannot inject a valid key without modifying storage";
}
