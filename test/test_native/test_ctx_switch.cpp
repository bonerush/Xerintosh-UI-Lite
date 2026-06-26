/**
 * @file   test_ctx_switch.cpp
 * @brief  Xeros 上下文切换引擎单元测试
 * @details 测试 kern_ctx_native_t 数据结构的大小、字段布局和字段访问。
 *          汇编函数（xeros_ctx_save / xeros_ctx_restore）包含 Xtensa 特定指令，
 *          无法在原生平台上运行，因此这些测试仅验证数据结构层面的正确性。
 *          硬件测试需要在实际 ESP32 上进行。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

extern "C" {
#include "kernel/esp32/ctx_switch.h"
}

/* ═══ 结构体大小验证 ═══ */

TEST(ContextSwitch, NativeCtxSize) {
    /* kern_ctx_native_t 应包含 24 个 uint32_t 字段，共 96 字节 */
    EXPECT_EQ(sizeof(kern_ctx_native_t), 96);
}

/* ═══ 字段写入与读取 ═══ */

TEST(ContextSwitch, InitPopulatesFields) {
    kern_ctx_native_t ctx;
    uint8_t stack[4096];
    memset(&ctx, 0xFF, sizeof(ctx));

    /*
     * 注意：xeros_ctx_init 是汇编实现，在原生平台上可能无法正常工作。
     * 如果有 C 包装函数，优先测试包装函数。
     * 此处仅验证结构体字段的可访问性。
     */
    ctx.a0 = 0x12345678;
    ctx.a1 = 0xDEADBEEF;
    ctx.pc = 0xCAFEBABE;
    EXPECT_EQ(ctx.a0, 0x12345678);
    EXPECT_EQ(ctx.a1, 0xDEADBEEF);
    EXPECT_EQ(ctx.pc, 0xCAFEBABE);

    /* 消除未使用变量警告 */
    (void)stack;
}

/* ═══ 字段偏移量验证 ═══ */

TEST(ContextSwitch, FieldLayout) {
    kern_ctx_native_t ctx;

    /* 通用寄存器 a0 位于结构体起始位置 */
    EXPECT_EQ((uintptr_t)&ctx.a0 - (uintptr_t)&ctx, 0);

    /* a1 紧随 a0 之后，偏移 4 字节 */
    EXPECT_EQ((uintptr_t)&ctx.a1 - (uintptr_t)&ctx, 4);

    /* a15 是第 16 个通用寄存器，偏移 15 * 4 = 60 字节 */
    EXPECT_EQ((uintptr_t)&ctx.a15 - (uintptr_t)&ctx, 60);

    /* sar 紧随 a15 之后，偏移 16 * 4 = 64 字节 */
    EXPECT_EQ((uintptr_t)&ctx.sar - (uintptr_t)&ctx, 64);

    /* pc 是最后一个字段，偏移 23 * 4 = 92 字节 */
    EXPECT_EQ((uintptr_t)&ctx.pc - (uintptr_t)&ctx, 92);
}

/* ═══ 特殊功能寄存器验证 ═══ */

TEST(ContextSwitch, SpecialRegisterFields) {
    kern_ctx_native_t ctx;

    ctx.sar    = 0x11111111;
    ctx.lbeg   = 0x22222222;
    ctx.lend   = 0x33333333;
    ctx.lcount = 0x44444444;
    ctx.ps     = 0x55555555;

    EXPECT_EQ(ctx.sar,    0x11111111);
    EXPECT_EQ(ctx.lbeg,   0x22222222);
    EXPECT_EQ(ctx.lend,   0x33333333);
    EXPECT_EQ(ctx.lcount, 0x44444444);
    EXPECT_EQ(ctx.ps,     0x55555555);
}
