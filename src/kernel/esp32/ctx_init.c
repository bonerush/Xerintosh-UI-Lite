/* Only compile for Xtensa targets (ESP32), skip on native test builds
 * and XEROS_NATIVE_SCHED fallback builds (which use FreeRTOS tasks). */
#if !defined(NATIVE_TEST) && !defined(XEROS_NATIVE_SCHED)

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
#include "../debug_serial.h"

#include <string.h>   /* memset */

/* ========================================================================== */
/*  外部符号声明                                                                */
/* ========================================================================== */

/**
 * @brief 任务蹦床符号（定义在 ctx_switch.S 中）
 *
 * ctx_restore_impl 通过 pc == xeros_task_trampoline 判断是否为新任务。
 * 蹦床通过 call8 调用 xeros_task_wrapper，使用 CALLINC=2 窗口旋转。
 * 蹦床符号仅用于新/旧任务判断。
 */
extern void xeros_task_trampoline(void);
extern void xeros_task_cleanup_handler(void);

/**
 * @brief 任务入口包装函数
 *
 * 由蹦床通过 call8 调用。使用默认 call8 ABI 编译，与蹦床和任务入口函数
 * 保持一致，避免混合 call4/call8 导致的窗口状态损坏。
 *
 * @param pxCode        任务入口函数指针
 * @param pvParameters  传递给任务入口函数的参数
 */
void xeros_task_wrapper(void (*pxCode)(void *), void *pvParameters)
{
    debug_printf("[WRAPPER] entering pxCode=%p pv=%p\n", (void *)pxCode, pvParameters);
    pxCode(pvParameters);
    debug_printf("[WRAPPER] pxCode returned, calling kern_exit\n");
    kern_exit();
}

/* DEBUG: ultra-minimal test function called directly from trampoline.
 * Bypasses the wrapper to test if trampoline → function → cleanup works. */
extern volatile uint32_t g_trampoline_reached;
void xeros_test_minimal(void)
{
    g_trampoline_reached = 42;
    /* Jump directly to cleanup handler (restores scheduler context) */
    extern void xeros_task_cleanup_handler(void);
    xeros_task_cleanup_handler();
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

#endif /* !NATIVE_TEST && !XEROS_NATIVE_SCHED */
