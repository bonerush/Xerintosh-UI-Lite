/**
 * @file   test_ui_dirty_region.cpp
 * @brief  脏矩形区域追踪测试
 * @details 验证脏标记从 bool 升级到 xerintosh_dirty_region_t 后，
 *          全屏/区域 invalidation 与清除行为正确。
 */

#include <gtest/gtest.h>

extern "C" {
#include "ui/ui_context.h"
#include "ui/ui_dirty.h"
#include "hal/hal_system.h"
#include "hal/hal_display.h"
}

class UiDirtyRegionTest : public ::testing::Test {
protected:
    void SetUp() override {
        xerintosh_context_init();
        hal_system_init();
        hal_display_init();
    }
};

TEST_F(UiDirtyRegionTest, FullScreenInvalidateCoversWholeScreen)
{
    xerintosh_invalidate();

    EXPECT_TRUE(xerintosh_is_dirty());

    const xerintosh_dirty_region_t *r = xerintosh_get_dirty_region();
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->active);
    EXPECT_EQ(r->x, 0);
    EXPECT_EQ(r->y, 0);
    EXPECT_EQ(r->w, HAL_SCREEN_WIDTH);
    EXPECT_EQ(r->h, HAL_SCREEN_HEIGHT);
}

TEST_F(UiDirtyRegionTest, ClearDirtyDeactivatesRegion)
{
    xerintosh_invalidate();
    xerintosh_clear_dirty();

    EXPECT_FALSE(xerintosh_is_dirty());

    const xerintosh_dirty_region_t *r = xerintosh_get_dirty_region();
    ASSERT_NE(r, nullptr);
    EXPECT_FALSE(r->active);
}

TEST_F(UiDirtyRegionTest, RegionInvalidateStoresBounds)
{
    xerintosh_clear_dirty();
    xerintosh_invalidate_region(10, 20, 30, 40);

    EXPECT_TRUE(xerintosh_is_dirty());

    const xerintosh_dirty_region_t *r = xerintosh_get_dirty_region();
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->active);
    EXPECT_EQ(r->x, 10);
    EXPECT_EQ(r->y, 20);
    EXPECT_EQ(r->w, 30);
    EXPECT_EQ(r->h, 40);
}

TEST_F(UiDirtyRegionTest, MultipleRegionsMergeToUnion)
{
    xerintosh_clear_dirty();
    xerintosh_invalidate_region(10, 10, 20, 20);
    xerintosh_invalidate_region(40, 40, 20, 20);

    const xerintosh_dirty_region_t *r = xerintosh_get_dirty_region();
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->active);
    EXPECT_EQ(r->x, 10);
    EXPECT_EQ(r->y, 10);
    EXPECT_EQ(r->w, 50);
    EXPECT_EQ(r->h, 50);
}

TEST_F(UiDirtyRegionTest, FullScreenInvalidationOverridesPreviousRegion)
{
    xerintosh_invalidate_region(10, 10, 20, 20);
    xerintosh_invalidate();

    const xerintosh_dirty_region_t *r = xerintosh_get_dirty_region();
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->x, 0);
    EXPECT_EQ(r->y, 0);
    EXPECT_EQ(r->w, HAL_SCREEN_WIDTH);
    EXPECT_EQ(r->h, HAL_SCREEN_HEIGHT);
}
