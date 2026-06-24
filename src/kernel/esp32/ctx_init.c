/* Only compile for Xtensa targets (ESP32), skip on native test builds */
#ifndef NATIVE_TEST

/**
 * @file ctx_init.c
 * @brief Xeros 上下文初始化辅助函数 — C 语言实现
 *
 * 本文件提供上下文初始化的 C 语言封装，与 ctx_switch.S 中的汇编实现
 * 功能等价。此外还提供处理器状态寄存器 (PS) 的读写辅助函数。
 *
 * 这些函数使得内核中不需要直接操作汇编即可完成新任务的上下文初始化。
 */

#include "ctx_switch.h"
#include "../kern_types.h"

#include <string.h>   /* memset */

/* ========================================================================== */
/*  外部符号声明                                                                */
/* ========================================================================== */

/**
 * @brief 任务蹦床函数（定义在 ctx_switch.S 中）
 *
 * 当任务入口函数正常返回时，执行流将跳转到此函数。
 * 该蹦床会调用 kern_exit() 以正确终止任务并释放资源。
 */
extern void xeros_task_trampoline(void);

/* ========================================================================== */
/*  处理器状态寄存器 (PS) 读写辅助                                               */
/* ========================================================================== */

/**
 * @brief 读取当前处理器状态寄存器 (PS) 的值
 *
 * 使用 Xtensa 的 rsr（Read Special Register）指令从 PS 特殊寄存器
 * 中读取当前值并返回。
 *
 * @return 当前 PS 寄存器的值
 *
 * @note PS 寄存器包含以下关键字段：
 *       - INTLEVEL [3:0]：当前中断级别
 *       - EXCM [4]：异常模式标志
 *       - UM [5]：用户模式标志（1=用户模式，0=特权模式）
 *       - WOE [18]：窗口溢出异常使能
 *       - CALLINC [17:16]：调用窗口增量（windowed ABI 使用）
 *       - OWB [11:8]：溢出窗口基址
 */
uint32_t xeros_get_ps(void)
{
    uint32_t ps;
    __asm__ __volatile__(
        "rsr %0, PS"
        : "=r"(ps)   /* 输出：将 PS 的值存入 ps 变量 */
        :             /* 无输入 */
        : "memory"    /* 内存屏障：防止编译器重排序 */
    );
    return ps;
}

/**
 * @brief 写入处理器状态寄存器 (PS)
 *
 * 使用 Xtensa 的 wsr（Write Special Register）指令将给定值写入
 * PS 特殊寄存器，随后执行 rsync 以确保写入立即生效。
 *
 * @param ps 要写入 PS 寄存器的值
 *
 * @note 写入 PS 后必须执行 rsync，因为 PS 的更改不会立即影响后续指令。
 *       rsync 会刷新流水线，确保新的 PS 值在下一条指令执行前生效。
 */
void xeros_set_ps(uint32_t ps)
{
    __asm__ __volatile__(
        "wsr %0, PS\n\t"
        "rsync"
        :             /* 无输出 */
        : "r"(ps)     /* 输入：将 ps 变量的值传入 */
        : "memory"    /* 内存屏障 */
    );
}

/* ========================================================================== */
/*  上下文初始化（C 封装版）                                                     */
/* ========================================================================== */

/**
 * @brief 初始化新任务的上下文结构体（C 语言封装）
 *
 * 本函数是 ctx_switch.S 中 xeros_ctx_init 汇编实现的 C 等价物。
 * 它将上下文结构体清零后，设置栈指针、蹦床入口和任务参数，
 * 使上下文可以被 xeros_ctx_restore() 首次启动。
 *
 * 新任务启动流程：
 *   xeros_ctx_restore() → 蹦床 (xeros_task_trampoline) → entry(arg) → kern_exit(0)
 *
 * 上下文布局：
 *   ctx->a0  = xeros_task_trampoline  (entry 返回后的着陆点)
 *   ctx->a1  = 对齐后的栈顶
 *   ctx->a5  = entry  (任务入口函数，蹦床通过 call8 a5 调用)
 *   ctx->a6  = arg    (任务参数，蹦床通过 a10 传递)
 *   ctx->pc  = xeros_task_trampoline  (首次恢复时的执行入口)
 *   ctx->ps  = 0x00020020 (用户模式，中断使能，CALLINC=2 for call8)
 *
 * @param[out] ctx         指向要初始化的上下文结构体
 * @param[in]  stack_base  栈内存的起始地址（低地址端）
 * @param[in]  stack_size  栈的大小（字节数）
 * @param[in]  entry       上下文启动后要执行的入口函数
 * @param[in]  arg         传递给入口函数的第一个参数
 *
 * @note 栈指针按 16 字节对齐（Xtensa ABI 强制要求）。
 * @note 蹦床使用 call8 ABI：entry 在 a13，arg 在 a14（窗口旋转后）。
 */
void xeros_ctx_init_assembler(kern_ctx_native_t *ctx,
                              void *stack_base,
                              size_t stack_size,
                              void (*entry)(void *),
                              void *arg)
{
    memset(ctx, 0, sizeof(kern_ctx_native_t));

    /* 栈顶 = 栈基址 + 栈大小，向下对齐到 16 字节边界（Xtensa ABI 要求） */
    uint32_t stack_top = ((uint32_t)stack_base + stack_size) & ~(uint32_t)0xF;
    ctx->a1 = stack_top;

    /* a0 = 蹦床：entry() 返回后的着陆点（call8 ABI 返回地址寄存器） */
    ctx->a0 = (uint32_t)xeros_task_trampoline;

    /* callx8 + entry sp,32 双重窗口旋转后的寄存器映射：
     *
     * callx8 a5 旋转窗口 +8：callee a1 = trampoline a9, callee a2 = trampoline a10
     * entry sp,32 再旋转 +8：总计 +16 = 回到原始窗口
     * 因此 callee 最终看到的 a1 = ctx->a9, a2 = ctx->a2
     *
     * 必须正确设置 a2（参数）和 a9（栈顶），否则 callee 收到错误的参数和栈指针。 */

    /* a2 = arg：callx8+entry 双重旋转后 callee 的 a2 映射到 ctx->a2 */
    ctx->a2  = (uint32_t)arg;

    /* a5 = entry, a6 = arg：蹦床通过这些寄存器调用 entry(arg)。
     * 同时存入 a13/a14：xeros_ctx_restore 从偏移 52/56 加载 a13/a14。 */
    ctx->a5  = (uint32_t)entry;
    ctx->a6  = (uint32_t)arg;
    ctx->a13 = (uint32_t)entry;
    ctx->a14 = (uint32_t)arg;

    /* a9 = 栈顶：callx8 旋转后 callee 的 a1 = trampoline a9，
     * entry sp,32 会执行 sp = a1 - 32，因此 a9 必须是栈顶地址。
     * 这样 callee 获得 sp = stack_top - 32（正确的满递减栈初始位置）。 */
    ctx->a9  = stack_top;

    /* PC = 蹦床：xeros_ctx_restore() 首次恢复时跳转到蹦床 */
    ctx->pc = (uint32_t)xeros_task_trampoline;

    /* PS = 0x00000020：用户模式，中断使能，WOE=0。
     * WOE=0 禁用窗口溢出异常，防止上下文切换时触发异常。
     * 参考 FreeRTOS ESP32 port 的做法：使用 call0 ABI 或禁用 WOE。 */
    ctx->ps = 0x00000020;
}

#endif /* !NATIVE_TEST */
