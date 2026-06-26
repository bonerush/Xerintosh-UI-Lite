/**
 * @file   test_ctx_switch.cpp
 * @brief  Xeros 原生上下文类型单元测试
 * @details 验证 ESP32 原生调度器下 kern_ctx_native_t 被定义为 newlib 的
 *          jmp_buf，大小与布局符合 Xtensa windowed ABI 预期。
 *
 *          真正的上下文切换（setjmp/longjmp）在 ESP32 硬件上验证；原生
 *          平台无法执行 Xtensa 汇编，因此只测试类型层面。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <csetjmp>

extern "C" {
#include "kernel/esp32/ctx_switch.h"
}

/* ═══ 类型与大小验证 ═══ */

TEST(ContextSwitch, NativeCtxIsJmpBuf) {
    /* kern_ctx_native_t 就是 jmp_buf */
    EXPECT_EQ(sizeof(kern_ctx_native_t), sizeof(jmp_buf));
}

TEST(ContextSwitch, NativeCtxSizeMatchesXtensaWindowed) {
    /* Xtensa windowed ABI 的 jmp_buf 为 17 个 int：
     * 12 个窗口寄存器 + 4 个 save-area 字 + 1 个返回地址 */
    EXPECT_EQ(sizeof(kern_ctx_native_t), 17 * sizeof(int));
}

/* ═══ 基本可实例化验证 ═══ */

TEST(ContextSwitch, JmpBufCanBeZeroed) {
    kern_ctx_native_t ctx;
    for (size_t i = 0; i < sizeof(ctx) / sizeof(int); i++) {
        ctx[i] = 0;
    }
    EXPECT_EQ(ctx[0], 0);
}
