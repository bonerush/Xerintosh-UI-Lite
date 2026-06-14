/**
 * @file   test_svc_mgr_helper.cpp
 * @brief  Service manager helper BT lifecycle tests
 * @details Verifies that svc_mgr_bt_request_enable/disable properly
 *          delegate to bt_manager and track lazy-init state.
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/svc_mgr_helper.h"
#include "app/bluetooth/bt_manager.h"
}

TEST(SvcMgrHelperBTTest, RequestEnable_WhenDisabled_SetsLazyFlag)
{
    bt_mgr_test_set_enabled(false);
    bool lazy = false;
    svc_mgr_bt_request_enable(&lazy);
    EXPECT_TRUE(lazy);
}

TEST(SvcMgrHelperBTTest, RequestEnable_WhenEnabled_DoesNotSetLazyFlag)
{
    bt_mgr_test_set_enabled(true);
    bool lazy = false;
    svc_mgr_bt_request_enable(&lazy);
    EXPECT_FALSE(lazy);
}

TEST(SvcMgrHelperBTTest, RequestDisable_WhenLazyAndEnabled_ClearsFlag)
{
    bt_mgr_test_set_enabled(false);
    bool lazy = false;
    svc_mgr_bt_request_enable(&lazy);
    EXPECT_TRUE(lazy);

    bt_mgr_test_set_enabled(true);
    svc_mgr_bt_request_disable(&lazy);
    EXPECT_FALSE(lazy);
}

TEST(SvcMgrHelperBTTest, RequestDisable_WhenNotLazy_DoesNothing)
{
    bt_mgr_test_set_enabled(true);
    bool lazy = false;
    svc_mgr_bt_request_disable(&lazy);
    EXPECT_FALSE(lazy);
}

TEST(SvcMgrHelperBTTest, RequestEnable_NullLazy_DoesNothing)
{
    bt_mgr_test_set_enabled(false);
    svc_mgr_bt_request_enable(nullptr);
}
