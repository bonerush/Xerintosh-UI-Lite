/* Only compile for Xtensa targets (ESP32) with native scheduler enabled.
 * Skip on native test builds (ucontext) and FreeRTOS backend builds. */
#if !defined(NATIVE_TEST) && defined(XEROS_NATIVE_SCHED)

/**
 * @file smp_ipi.S
 * @brief Xeros 原生调度器 IPI（处理器间中断）处理桩
 *
 * ESP32 的 esp_ipc_isr_call() 在目标核心上以高优先级中断上下文执行本回调。
 * 回调必须满足：
 *   - 使用 call0 ABI（由 CALLX0 调用）
 *   - 不能使用窗口旋转指令（entry/retw）
 *   - 只能使用 a2/a3/a4 寄存器
 *   - 必须放在 IRAM 中
 *
 * 本处理函数仅设置目标核心的 g_per_cpu[cpu].need_resched 标志，
 * 让目标核心尽快退出 idle 并重新调度。
 */

    .file   "smp_ipi.S"

    .section .iram1.text, "ax"
    .align  4
    .global xeros_ipi_reschedule_handler
    .type   xeros_ipi_reschedule_handler, @function

/**
 * @brief IPI 重新调度处理函数
 * @param a2 指向目标核心 need_resched 标志的指针 (volatile bool *)
 */
xeros_ipi_reschedule_handler:
    /* 将 need_resched 标志写为 true */
    movi    a3, 1
    s8i     a3, a2, 0
    ret

    .size   xeros_ipi_reschedule_handler, . - xeros_ipi_reschedule_handler

#endif /* !NATIVE_TEST && XEROS_NATIVE_SCHED */
