#include <gtest/gtest.h>

extern "C" {
#include "app/user_item_contract.h"
#include "app/taskmgr/taskmgr.h"
#include "app/serial_monitor/serial_monitor.h"
#include "app/token_usage/token_usage.h"
#include "app/flasher/flasher.h"
#include "app/oscilloscope/oscilloscope.h"
#include "app/about/about.h"
}

TEST(UserItemContractTest, AllBuiltinAppsMatchContract)
{
    const user_item_contract_t contracts[] = {
        {"任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit},
        {"串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit},
        {"Token 消耗", token_usage_init, token_usage_loop, token_usage_exit},
        {"烧录器", flasher_init, flasher_loop, flasher_exit},
        {"示波器", oscilloscope_init, oscilloscope_loop, oscilloscope_exit},
        {"关于", about_init, about_loop, about_exit},
    };

    for (size_t i = 0; i < sizeof(contracts) / sizeof(contracts[0]); i++) {
        EXPECT_NE(contracts[i].init, nullptr);
        EXPECT_NE(contracts[i].loop, nullptr);
        EXPECT_NE(contracts[i].exit, nullptr);
    }
}
