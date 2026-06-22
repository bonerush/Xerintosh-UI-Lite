#include <gtest/gtest.h>
#include "app/token_usage/tu_api.h"

TEST(TuApiTest, DataInit) {
    tu_data_t data;
    tu_data_init(&data);

    EXPECT_FLOAT_EQ(data.deepseek.total_balance, 0.0f);
    EXPECT_FLOAT_EQ(data.deepseek.granted_balance, 0.0f);
    EXPECT_FLOAT_EQ(data.deepseek.topped_up_balance, 0.0f);
    EXPECT_FALSE(data.deepseek_ok);
}

TEST(TuApiTest, DeepseekFetchStub) {
    tu_deepseek_balance_t balance;
    bool ok = tu_api_fetch_deepseek("test_key", &balance);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(balance.is_available);
    EXPECT_FLOAT_EQ(balance.total_balance, 4.95f);
}

TEST(TuApiTest, NullPointerGuards) {
    // Should not crash with null inputs
    EXPECT_FALSE(tu_api_fetch_deepseek(NULL, NULL));

    tu_data_t data;
    tu_data_init(NULL);  // Should not crash
}
