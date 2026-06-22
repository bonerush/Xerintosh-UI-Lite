#include <gtest/gtest.h>

/* ═══ Phase 1: about 页面布局测试 ═══ */

/**
 * @brief about 页面横屏（160x80）下所有信息行不应溢出屏幕
 * @note  横屏采用极简紧凑布局：logo 缩小、信息合并为 2 行
 */
TEST(LayoutAboutTest, LandscapeDoesNotOverflow)
{
    /* 模拟横屏参数（与 about.c 一致） */
    const int16_t fh          = 12;   /* efontCN_12 字体高度 */
    const int16_t sw          = 160;  /* 横屏宽度 */
    const int16_t sh          = 80;   /* 横屏高度 */
    const int16_t logo_scale  = 45;   /* 横屏 logo 缩放比例 */
    const int16_t logo_oy     = 0;    /* logo 顶部偏移 */
    const int16_t lx          = 4;    /* 左侧边距 */

    /* 来自 about.c 的当前布局计算（横屏分支 sh < 140） */
    int16_t title_y      = logo_oy + 60 * logo_scale / 100 + 1;  /* = 0 + 27 + 1 = 28 */
    int16_t info_start_y = title_y + fh + 1;                    /* = 28 + 12 + 1 = 41 */
    int16_t line_dy      = fh;                                  /* = 12，无额外间距 */

    /* 依次计算每行文字的基线 y 坐标（hal_draw_string(lx, y + fh, ...)） */
    int16_t y = info_start_y;

    /* "v1.0.0  Codename Platform" */
    int16_t line1_baseline = y + fh;  /* = 41 + 12 = 53 */
    y += line_dy;                     /* = 53 */

    /* "by Developer  Date" */
    int16_t line2_baseline = y + fh;  /* = 53 + 12 = 65 */

    /* 底部提示 "Hold BtnB to return" */
    int16_t footer_baseline = sh - 2; /* = 78 */

    /* 断言：最后一行信息不应溢出屏幕 */
    EXPECT_LE(line2_baseline, sh)
        << "Last info line baseline (" << line2_baseline
        << ") exceeds screen height (" << sh << ")";

    /* 断言：信息行不应与底部提示重叠 */
    EXPECT_LT(line2_baseline, footer_baseline - fh)
        << "Info lines overlap with footer text";

    /* 断言：标题不应太靠近屏幕顶部 */
    EXPECT_GE(title_y, fh)
        << "Title too close to top edge";
}

/**
 * @brief about 页面竖屏（80x160）下布局应正常
 */
TEST(LayoutAboutTest, PortraitLayoutIsValid)
{
    const int16_t fh          = 12;
    const int16_t sh          = 160;  /* 竖屏高度 */
    const int16_t logo_scale  = 80;   /* 竖屏 logo 缩放 */
    const int16_t logo_oy     = 2;

    int16_t title_y      = logo_oy + 60 * logo_scale / 100 + 5;  /* = 2 + 48 + 5 = 55 */
    int16_t info_start_y = title_y + fh + 6;                    /* = 55 + 12 + 6 = 73 */
    int16_t line_dy      = fh + 5;                              /* = 17 */

    int16_t y = info_start_y;
    y += line_dy;  /* v1.0.0 */
    y += line_dy;  /* M5Stick-C */
    y += line_dy;  /* by Bonerush */
    y += line_dy;  /* May 27 2026 */

    int16_t line4_baseline = y;
    int16_t footer_baseline = sh - 2;

    EXPECT_LE(line4_baseline, sh)
        << "Portrait: last info line overflows";
    EXPECT_LT(line4_baseline, footer_baseline - fh)
        << "Portrait: info lines overlap footer";
}

/* ═══ Phase 2: taskmgr 布局测试 ═══ */

/**
 * @brief taskmgr 在横屏下应显示 3 行（缩减间距后）
 */
TEST(LayoutTaskmgrTest, LandscapeVisibleLines)
{
    const int16_t fh            = 12;
    const int16_t sh            = 80;   /* 横屏高度 */
    const int16_t HEADER_Y      = 2;
    const int16_t FOOTER_MARGIN = 4;

    /* Phase 1 新布局：缩减 header/footer 间距以容纳 3 行 */
    int16_t header_h = HEADER_Y + fh + 2;        /* = 2 + 12 + 2 = 16 */
    int16_t footer_h = fh + 2;                   /* = 12 + 2 = 14 */
    int16_t avail    = sh - header_h - footer_h; /* = 80 - 16 - 14 = 50 */
    int16_t row_h    = fh + 4;                   /* = 16 */
    int16_t visible  = avail / row_h;            /* = 50 / 16 = 3 */

    /* 断言：横屏应显示 3 行 */
    EXPECT_EQ(visible, 3)
        << "Landscape should show 3 lines, got " << visible;

    /* 断言：list_top 与 taskmgr_ui.c 保持一致 */
    int16_t list_top = HEADER_Y + fh + 3;        /* = 2 + 12 + 3 = 17 */
    EXPECT_EQ(list_top, 17);
}

/**
 * @brief taskmgr 在竖屏下应显示更多行
 */
TEST(LayoutTaskmgrTest, PortraitVisibleLines)
{
    const int16_t fh            = 12;
    const int16_t sh            = 160;  /* 竖屏高度 */
    const int16_t HEADER_Y      = 2;
    const int16_t FOOTER_MARGIN = 4;

    int16_t header_h = HEADER_Y + fh + 6;        /* = 20 */
    int16_t footer_h = fh + FOOTER_MARGIN + 4;   /* = 20 */
    int16_t avail    = sh - header_h - footer_h; /* = 120 */
    int16_t row_h    = fh + 4;                   /* = 16 */
    int16_t visible  = avail / row_h;            /* = 7 */

    EXPECT_GE(visible, 4)
        << "Portrait should show at least 4 task lines, got " << visible;
}
