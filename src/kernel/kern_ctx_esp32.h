/**
 * @file   kern_ctx_esp32.h
 * @brief  Xtensa (ESP32) setjmp/longjmp 上下文切换原语
 * @details 为 XEROS_NATIVE_SCHED 模式提供轻量级上下文切换。
 *
 *          使用标准 C 的 setjmp/longjmp（newlib 实现）保存和恢复
 *          寄存器状态，配合 SP 切换实现独立任务栈。
 *
 *          ⚠️ 实验性特性：默认不启用。需手动 #define XEROS_NATIVE_SCHED。
 *             Xtensa 窗口寄存器架构使上下文切换比 ARM/x86 更复杂；
 *             FreeRTOS 包装器模式（默认）更稳定可靠。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_CTX_ESP32_H
#define KERN_CTX_ESP32_H

#ifndef NATIVE_TEST

#include "kern_types.h"
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 类型定义 ═══ */

/** Xtensa 任务上下文：jmp_buf + 保存的 SP */
typedef struct {
    jmp_buf  jmp;
    void    *sp;       /* 保存的栈指针（用于初始化时恢复） */
} kern_ctx_t;

/* ═══ 上下文操作 ═══ */

/**
 * @brief  初始化任务上下文
 * @param  ctx       上下文对象
 * @param  stack     预分配的栈底地址
 * @param  stack_top 栈顶地址（SP 初始值）
 * @param  entry     任务入口函数
 * @param  arg       入口参数
 * @note   调用者负责分配和管理栈内存。
 *         此函数在调用者栈上执行，通过内联汇编临时切换到任务栈
 *         以调用 setjmp 捕获初始上下文。
 */
static inline void kern_ctx_init(kern_ctx_t *ctx,
                                 uint8_t *stack,
                                 uint8_t *stack_top,
                                 void (*entry)(void *arg),
                                 void *arg)
{
    (void)stack;
    if (ctx == NULL || entry == NULL) return;

    void *caller_sp;
    __asm__ volatile("mov %0, a1" : "=r"(caller_sp));

    /*
     * 切换到任务栈，调用 setjmp 记录上下文，再切回调用者栈。
     *
     * Xtensa 注意事项：
     * - A1 = SP (栈指针)
     * - A0 = 返回地址（由 setjmp 保存到 jmp_buf）
     * - setjmp 保存 A0-A1 和 A12-A15（callee-saved）
     * - 窗口溢出由 Xtensa WindowOverflow 异常处理器自动管理
     */
    __asm__ volatile(
        "mov  a1, %[new_sp]  \n\t"  /* 切换到任务栈 */
        "mov  a3, %[entry]   \n\t"  /* entry → A3 (暂存) */
        "mov  a4, %[arg]     \n\t"  /* arg   → A4 (暂存) */
        "mov  a5, %[jmp]     \n\t"  /* jmp_buf 地址 → A5 */
        "mov  a6, %[sj]      \n\t"  /* setjmp 函数地址 → A6 */
        "callx8 a6           \n\t"  /* call setjmp(jmp_buf) */
        "bnez a2, 1f         \n\t"  /* 如果 setjmp 返回非 0（longjmp 回到此处）*/
        /* setjmp 返回 0：保存成功，恢复调用者 SP */
        "mov  a1, %[old_sp]  \n\t"
        "j    2f             \n\t"
        /* setjmp 返回 1（来自 longjmp）：进入任务入口 */
        "1:                  \n\t"
        "mov  a2, a4         \n\t"  /* arg 放入 A2 (参数寄存器) */
        "callx8 a3           \n\t"  /* 调用 entry(arg) */
        /* entry 不应返回；若返回则进入死循环 */
        "3:                  \n\t"
        "j    3b             \n\t"
        "2:                  \n\t"
        : /* 无输出 */
        : [new_sp] "r"(stack_top),
          [old_sp] "r"(caller_sp),
          [entry]  "r"(entry),
          [arg]    "r"(arg),
          [jmp]    "r"(&ctx->jmp),
          [sj]     "r"(setjmp)
        : "a2", "a3", "a4", "a5", "a6", "memory"
    );

    ctx->sp = NULL; /* ESP32 模式下 SP 已由 setjmp 管理 */
}

/**
 * @brief 保存当前上下文（包装 setjmp）
 * @return 0 = 首次调用（已保存），非 0 = 从 longjmp 恢复
 */
static inline int kern_ctx_save(kern_ctx_t *ctx)
{
    return setjmp(ctx->jmp);
}

/**
 * @brief 切换到目标上下文（包装 longjmp）
 * @note  调用此函数将直接跳转到目标上下文的 setjmp 返回点，
 *        此函数不返回。
 */
static inline void kern_ctx_switch_to(kern_ctx_t *ctx)
{
    longjmp(ctx->jmp, 1);
}

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_TEST */
#endif /* KERN_CTX_ESP32_H */
