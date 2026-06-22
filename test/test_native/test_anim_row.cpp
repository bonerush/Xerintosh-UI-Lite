/**
 * @file   test_anim_row.cpp
 * @brief  ui_anim_row 行列表动画工具单元测试
 */

#include <gtest/gtest.h>
#include <math.h>

/* 前向声明被测函数（直接包含 .c 文件不适合，这里手动声明接口） */
extern "C" {
#include "ui/ui_anim_row.h"
}

/* ═══ xerintosh_anim_row_list_init 测试 ═══ */

TEST(AnimRowInitTest, SetsInitialPositionsToScreenHeight)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 3, 16, 20);

    /* 所有行应从 SCREEN_HEIGHT（入场起点）开始 */
    for (int i = 0; i < 3; i++) {
        EXPECT_FLOAT_EQ(list.rows[i].y, (float)SCREEN_HEIGHT)
            << "Row " << i << " should start at SCREEN_HEIGHT";
    }

    /* 目标位置应为 list_top + i * row_height */
    EXPECT_FLOAT_EQ(list.rows[0].y_trg, 20.0f);
    EXPECT_FLOAT_EQ(list.rows[1].y_trg, 36.0f);
    EXPECT_FLOAT_EQ(list.rows[2].y_trg, 52.0f);

    /* 高亮框也应从 SCREEN_HEIGHT 开始 */
    EXPECT_FLOAT_EQ(list.highlight.y, (float)SCREEN_HEIGHT);
    EXPECT_FLOAT_EQ(list.highlight.y_trg, 20.0f);

    /* 可见行数和行高应正确存储 */
    EXPECT_EQ(list.visible_count, 3);
    EXPECT_EQ(list.row_height, 16);
    EXPECT_EQ(list.list_top, 20);
}

TEST(AnimRowInitTest, SetsScrollOffsetToZero)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 5, 20, 0);

    EXPECT_FLOAT_EQ(list.scroll_offset, 0.0f);
    EXPECT_FLOAT_EQ(list.scroll_offset_trg, 0.0f);
}

/* ═══ xerintosh_anim_row_list_update 测试 ═══ */

TEST(AnimRowUpdateTest, AnimatesTowardTarget)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 2, 20, 0);

    /* 设置行从 SCREEN_HEIGHT 向 0 移动 */
    list.rows[0].y = (float)SCREEN_HEIGHT;
    list.rows[0].y_trg = 0.0f;
    list.rows[1].y = (float)SCREEN_HEIGHT;
    list.rows[1].y_trg = 20.0f;

    /* 更新一帧（speed=90 快速移动） */
    xerintosh_anim_row_list_update(&list, 90.0f);

    /* 行应该向目标移动 */
    EXPECT_LT(list.rows[0].y, (float)SCREEN_HEIGHT)
        << "Row 0 should have moved toward target";
    EXPECT_GT(list.rows[0].y, 0.0f)
        << "Row 0 should not overshoot to negative";
}

TEST(AnimRowUpdateTest, SettlesWhenCloseEnough)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 1, 20, 0);

    list.rows[0].y = 10.3f;
    list.rows[0].y_trg = 10.0f;
    /* 高亮框和滚动已到位，确保不干扰 settled 判断 */
    list.highlight.y = list.highlight.y_trg;
    list.highlight.w = list.highlight.w_trg;

    /* diff < 1.0 时应 snap 到目标（xerintosh_animation 行为） */
    bool settled = xerintosh_anim_row_list_update(&list, 90.0f);

    EXPECT_NEAR(list.rows[0].y, 10.0f, 0.01f);
    EXPECT_TRUE(settled);
}

TEST(AnimRowUpdateTest, ReturnsTrueWhenAllSettled)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 2, 20, 0);

    /* 所有行已到位 */
    list.rows[0].y = 0.0f;
    list.rows[0].y_trg = 0.0f;
    list.rows[1].y = 20.0f;
    list.rows[1].y_trg = 20.0f;
    list.highlight.y = 0.0f;
    list.highlight.y_trg = 0.0f;
    list.highlight.w = 80.0f;
    list.highlight.w_trg = 80.0f;
    list.scroll_offset = 0.0f;
    list.scroll_offset_trg = 0.0f;

    bool settled = xerintosh_anim_row_list_update(&list, 50.0f);

    EXPECT_TRUE(settled);
}

/* ═══ xerintosh_anim_row_list_refresh 测试 ═══ */

TEST(AnimRowRefreshTest, CalculatesTargetsForVisibleRows)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 3, 16, 10);

    /* 刷新：selected=1, scroll=0, screen=80, item_count=5 */
    xerintosh_anim_row_list_refresh(&list, 1, 0, 80, 5);

    /* 可见行目标应 = list_top + i * row_height */
    EXPECT_FLOAT_EQ(list.rows[0].y_trg, 10.0f);
    EXPECT_FLOAT_EQ(list.rows[1].y_trg, 26.0f);
    EXPECT_FLOAT_EQ(list.rows[2].y_trg, 42.0f);

    /* 高亮框应跟踪 selected=1（第 1 个可见行） */
    EXPECT_FLOAT_EQ(list.highlight.y_trg, 26.0f);
    EXPECT_FLOAT_EQ(list.highlight.w_trg, 80.0f);
}

TEST(AnimRowRefreshTest, HandlesScrollOffset)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 3, 16, 10);

    /* scroll=2：可见行是索引 2,3,4 */
    xerintosh_anim_row_list_refresh(&list, 3, 2, 80, 6);

    /* 所有可见行（0,1,2 → 实际索引 2,3,4）都有有效目标 */
    EXPECT_FLOAT_EQ(list.rows[0].y_trg, 10.0f);
    EXPECT_FLOAT_EQ(list.rows[1].y_trg, 26.0f);
    EXPECT_FLOAT_EQ(list.rows[2].y_trg, 42.0f);

    /* 高亮跟踪 selected=3，可见索引是 3-2=1 */
    EXPECT_FLOAT_EQ(list.highlight.y_trg, 26.0f);

    /* scroll_offset_trg 应跟随 scroll */
    EXPECT_FLOAT_EQ(list.scroll_offset_trg, 2.0f);
}

TEST(AnimRowRefreshTest, OutOfBoundsItemsMoveOutOfView)
{
    xerintosh_anim_row_list_t list;
    xerintosh_anim_row_list_init(&list, 3, 16, 10);

    /* 只有 2 个 item，但可见 3 行 → 多出的行应移出视野 */
    xerintosh_anim_row_list_refresh(&list, 0, 0, 80, 2);

    /* 前 2 行正常 */
    EXPECT_FLOAT_EQ(list.rows[0].y_trg, 10.0f);
    EXPECT_FLOAT_EQ(list.rows[1].y_trg, 26.0f);
    /* 第 3 行越界，应移出视野（list_top + 3 * row_height） */
    EXPECT_FLOAT_EQ(list.rows[2].y_trg, 58.0f);
}
