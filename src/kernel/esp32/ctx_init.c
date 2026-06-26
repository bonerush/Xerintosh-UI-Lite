/* Only compile for Xtensa targets (ESP32) with native scheduler enabled.
 * Skip on native test builds (ucontext) and FreeRTOS backend builds. */
#if !defined(NATIVE_TEST) && defined(XEROS_NATIVE_SCHED)

/**
 * @file ctx_init.c
 * @brief Xeros 原生上下文初始化辅助函数 — 复用 newlib setjmp/longjmp
 *
 * 本文件提供：
 *   - xeros_ctx_save / xeros_ctx_restore：jmp_buf 包装。
 *   - xeros_task_wrapper：用户任务入口包装。
 *   - xeros_task_start：新任务首次运行的 C 包装，负责计算栈顶并调用
 *     汇编启动桩 xeros_task_start_asm。
 */

#include "ctx_switch.h"
#include "../kern_types.h"
#include "../kern_task.h"
#include "../debug_serial.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

int xeros_ctx_save(kern_ctx_native_t *ctx)
{
    return setjmp(*ctx);
}

__attribute__((noreturn)) void xeros_ctx_restore(kern_ctx_native_t *ctx)
{
    longjmp(*ctx, 1);
}

/**
 * @brief 任务入口包装函数（windowed ABI）
 *
 * 由启动桩调用。函数参数遵循 call8 ABI：a2 = pxCode，a3 = pvParameters。
 * 包装函数负责调用用户任务入口，并在任务自然返回时调用 kern_exit()。
 */
void xeros_task_wrapper(void (*pxCode)(void *), void *pvParameters)
{
    debug_printf("[native] task_wrapper: pxCode=%p\n", (void *)pxCode);
    pxCode(pvParameters);
    kern_exit();
}

/* 汇编启动桩：切换到指定栈顶并调用 xeros_task_wrapper(entry, arg) */
extern void xeros_task_start_asm(void *stack_top,
                                 void (*entry)(void *),
                                 void *arg);

/**
 * @brief 首次启动一个新任务（不返回）
 *
 * 计算任务栈顶，然后交给汇编桩完成栈切换并进入 xeros_task_wrapper。
 */
__attribute__((noinline, noreturn)) void xeros_task_start(kern_task_t *task)
{
    uintptr_t sp = (uintptr_t)(task->stack_base + task->stack_size);
    sp &= ~((uintptr_t)0xF);
    xeros_task_start_asm((void *)sp, task->entry, task->arg);
    __builtin_unreachable();
}

#endif /* !NATIVE_TEST && XEROS_NATIVE_SCHED */
