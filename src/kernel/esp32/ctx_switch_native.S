/* Only compile for Xtensa targets (ESP32) with native scheduler enabled.
 * Skip on native test builds (ucontext) and FreeRTOS backend builds. */
#if !defined(NATIVE_TEST) && defined(XEROS_NATIVE_SCHED)

/**
 * @file ctx_switch_native.S
 * @brief Xeros 原生调度器启动桩
 *
 * 上下文保存/恢复现在复用 newlib 的 setjmp/longjmp（见 ctx_init.c）。
 * 本文件只保留新任务首次运行所需的汇编启动桩：切换到任务私有栈顶并
 * 调用 xeros_task_wrapper(entry, arg)。
 */

    .file   "ctx_switch_native.S"

    .extern xeros_task_wrapper

    .section .text
    .align  4
    .global xeros_task_start_asm
    .type   xeros_task_start_asm, @function
xeros_task_start_asm:
    /* 窗口化 ABI 入口：a2 = stack_top, a3 = entry, a4 = arg */
    entry   sp, 32

    /* 切换到任务私有栈 */
    mov     a1, a2

    /* call8 的参数传递：调用者的 a10/a11 会成为被调函数的 a2/a3，
     * 因此把 wrapper 需要的参数放到 a10/a11。 */
    mov     a10, a3
    mov     a11, a4

    /* 进入包装函数；正常路径不会返回（yield/exit 都走 longjmp） */
    call8   xeros_task_wrapper

    /* 防坠保险：如果意外返回则死循环 */
.L_halt:
    j       .L_halt

    .size   xeros_task_start_asm, . - xeros_task_start_asm

#endif /* !NATIVE_TEST && XEROS_NATIVE_SCHED */
