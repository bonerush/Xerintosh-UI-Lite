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
#include "../kern_task.h"

#include <string.h>   /* memset */

/* ========================================================================== */
/*  外部符号声明                                                                */
/* ========================================================================== */

/**
 * @brief 任务蹦床符号（定义在 ctx_switch.S 中）
 *
 * ctx_restore_impl 通过 pc == xeros_task_trampoline 判断是否为新任务。
 * 蹦床通过 call4 调用 xeros_task_wrapper，使用 CALLINC=1 窗口旋转。
 * 蹦床符号仅用于新/旧任务判断。
 */
extern void xeros_task_trampoline(void);
extern void xeros_task_cleanup_handler(void);

/* ========================================================================== */
/*  任务包装函数                                                                */
/* ========================================================================== */

/**
 * @brief 任务包装函数 — 类似 FreeRTOS 的 vPortTaskWrapper
 *
 * 由蹦床通过 call4 调用，编译器生成 CALLINC=1 的 entry 指令。
 * 包装函数调用实际的任务入口函数，任务返回后调用 kern_exit() 清理。
 *
 * 调用链：
 *   蹦床 (call4) → 包装函数 (call4) → 任务入口函数
 *   窗口旋转：K → K+1(蹦床) → K+2(包装) → K+3(任务)
 *
 * @param pxCode      任务入口函数（call4 第一个参数，a0 旋转后）
 * @param pvParameters 任务参数（call4 第二个参数，a1 旋转后）
 */
void xeros_task_wrapper(void (*pxCode)(void *), void *pvParameters)
{
    pxCode(pvParameters);
    kern_exit();
}

/* ========================================================================== */
/*  处理器状态寄存器 (PS) 读写辅助                                               */
/* ========================================================================== */

uint32_t xeros_get_ps(void)
{
    uint32_t ps;
    __asm__ __volatile__(
        "rsr %0, PS"
        : "=r"(ps)
        :
        : "memory"
    );
    return ps;
}

void xeros_set_ps(uint32_t ps)
{
    __asm__ __volatile__(
        "wsr %0, PS\n\t"
        "rsync"
        :
        : "r"(ps)
        : "memory"
    );
}

/* ========================================================================== */
/*  上下文初始化（C 封装版）                                                     */
/* ========================================================================== */

/**
 * @brief 初始化新任务的上下文结构体
 *
 * 新任务启动流程（CALLINC=1，蹦床 + C 包装函数方案，类似 FreeRTOS）：
 *
 *   1. ctx_restore_impl 设置 PS.CALLINC=1, WOE=1, UM=1, INTLEVEL=3
 *   2. ctx_restore_impl 设置 WINDOWSTART=0xFFFF
 *   3. ctx_restore_impl 恢复 GPR 后 jx 到蹦床
 *   4. 蹦床: entry sp, 0 (CALLINC=1: 旋转窗口 +1)
 *   5. 蹦床: call4 xeros_task_wrapper
 *   6. 包装函数: entry a1, N (CALLINC=1: 旋转窗口 +1)
 *   7. 包装函数: pxCode(pvParameters) → 任务执行
 *   8. 任务返回后: kern_exit()
 *
 * 寄存器映射（ctx_restore_impl 加载后 → 蹦床 call4 后 → 包装函数看到）：
 *   ctx->a2 = 清理处理器地址 → 蹦床 a2 → 包装函数 a0 (retw 返回地址)
 *   ctx->a3 = 栈顶          → 蹦床 a3 → 包装函数 a1 (SP)
 *   ctx->a4 = entry 函数     → 蹦床 a4 → 包装函数 a2 (pxCode)
 *   ctx->a5 = arg            → 蹦床 a5 → 包装函数 a3 (pvParameters)
 *
 * @param[out] ctx         指向要初始化的上下文结构体
 * @param[in]  stack_base  栈内存的起始地址（低地址端）
 * @param[in]  stack_size  栈的大小（字节数）
 * @param[in]  entry       上下文启动后要执行的入口函数
 * @param[in]  arg         传递给入口函数的第一个参数
 */
void xeros_ctx_init_assembler(kern_ctx_native_t *ctx,
                              void *stack_base,
                              size_t stack_size,
                              void (*entry)(void *),
                              void *arg)
{
    memset(ctx, 0, sizeof(kern_ctx_native_t));

    uint32_t stack_top = ((uint32_t)stack_base + stack_size) & ~(uint32_t)0xF;

    /* 蹦床通过 call4 调用包装函数。call4 的寄存器映射：
     *   蹦床 a2 → 包装函数 a0 = 清理处理器（retw 返回地址）
     *   蹦床 a3 → 包装函数 a1 = 栈顶（SP）
     *   蹦床 a4 → 包装函数 a2 = pxCode（entry 函数）
     *   蹦床 a5 → 包装函数 a3 = pvParameters（arg） */
    ctx->a2 = (uint32_t)xeros_task_cleanup_handler;  /* → 包装函数 a0 = retw 返回地址 */
    ctx->a3 = stack_top;                              /* → 包装函数 a1 = SP */
    ctx->a4 = (uint32_t)entry;                        /* → 包装函数 a2 = pxCode */
    ctx->a5 = (uint32_t)arg;                          /* → 包装函数 a3 = pvParameters */

    ctx->pc = (uint32_t)xeros_task_trampoline;        /* 新/旧任务检测标记 */
    ctx->ps = 0x00040023;                             /* CALLINC=1, WOE=1, UM=1, INTLEVEL=3 */
}

#endif /* !NATIVE_TEST */
