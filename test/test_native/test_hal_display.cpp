/**
 * @file   test_hal_display.cpp
 * @brief  TDD RED 阶段 — 验证 hal_draw_utf8/hal_draw_string 和
 *         hal_get_utf8_width/hal_get_string_width 行为等价。
 *
 *         重构目标：将 utf8 变体改为 string 变体的宏别名，
 *         消除 4 个冗余函数，统一内部调用。
 *
 *         因 native 测试环境下文字绘制为桩实现，
 *         本测试聚焦于宽度计算等价和编译/链接兼容性。
 */

#include <gtest/gtest.h>

#include "hal/hal_display.h"

/* ═══ 宽度函数等价性 ═══ */

TEST(HalDisplayEquivalence, GetUtf8WidthEqualsGetStringWidth)
{
    /*
     * 两个函数接受相同类型参数、返回相同类型。
     * native 路径下两者均为返回 0 的桩 — 但我们在此验证行为一致。
     */
    const char *test_strings[] = {
        "hello",
        "你好世界",
        "abc123中文",
        "",              /* 空字符串 */
        NULL             /* 哨兵 */
    };

    for (int i = 0; test_strings[i] != NULL; i++) {
        int16_t w_utf8  = hal_get_utf8_width(test_strings[i]);
        int16_t w_ascii = hal_get_string_width(test_strings[i]);
        /* 核心断言：两者必须返回相同值 */
        EXPECT_EQ(w_utf8, w_ascii)
            << "width mismatch for '" << test_strings[i] << "'";
    }
}

TEST(HalDisplayEquivalence, NullPointerReturnsZeroWidth)
{
    EXPECT_EQ(hal_get_utf8_width(NULL), 0);
    EXPECT_EQ(hal_get_string_width(NULL), 0);
}

/* ═══ 编译/链接兼容性 ═══ */

TEST(HalDisplayEquivalence, DrawFunctionsLinkAndDontCrash)
{
    /*
     * 这两个调用仅验证函数可正常链接和调用。
     * native 路径下两者都是桩，但签名和行为必须一致。
     */
    hal_draw_utf8(0, 0, "test", 0xFFFF);
    hal_draw_string(0, 0, "test", 0xFFFF);
    SUCCEED();
}

TEST(HalDisplayEquivalence, BothAcceptSameParameterTypes)
{
    /*
     * 如果将来用宏替换 hal_draw_utf8，
     * 调用点不必做任何修改 — 此测试保证 ABI 兼容。
     */
    const char *msg = "ABI check";
    uint16_t    col = 0xABCD;

    hal_draw_utf8(10, 20, msg, col);
    hal_draw_string(10, 20, msg, col);

    int16_t w1 = hal_get_utf8_width(msg);
    int16_t w2 = hal_get_string_width(msg);

    /* 两者类型相同，赋值不会产生警告 */
    EXPECT_EQ(w1, w2);
}

/* ═══ 初始化可调用性 ═══ */

TEST(HalDisplayInit, InitDoesNotCrash)
{
    /*
     * native 路径下 hal_display_init() 为空操作/清空帧缓冲。
     * 验证 public 签名可用、不崩溃，确保 HAL 生命周期可正常调用。
     */
    hal_display_init();
    SUCCEED();
}

/* ═══ XBM 位序：MSB 在前（P1-6）═══ */

TEST(HalDisplayXbm, MsbFirstBitOrder)
{
    hal_display_init();

    /* 8x1 XBM，bitmap[0] = 0x80：仅最左侧像素应点亮 */
    static const uint8_t bitmap[] = { 0x80 };
    hal_draw_xbitmap(0, 0, 8, 1, bitmap);

    EXPECT_NE(hal_test_fb_read(0, 0), 0u);   /* (0,0) 应点亮 */
    EXPECT_EQ(hal_test_fb_read(1, 0), 0u);   /* (1,0) 应熄灭 */
    EXPECT_EQ(hal_test_fb_read(7, 0), 0u);   /* (7,0) 应熄灭 */
}
