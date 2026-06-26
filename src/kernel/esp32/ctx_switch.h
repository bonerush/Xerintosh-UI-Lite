/**
 * @file ctx_switch.h
 * @brief Xeros 原生上下文切换引擎 — ESP32 Xtensa 复用 newlib setjmp/longjmp
 *
 * 设计要点：
 *   - 调度器和用户任务都使用 GCC 默认的 windowed（call8）ABI 编译。
 *   - 任务上下文用 newlib 的 jmp_buf 保存/恢复，依赖其内部的 syscall
 *     完成窗口寄存器刷写，避免手写 retw 恢复时窗口下溢处理器的栈偏移问题。
 *   - 新任务第一次运行走专用的 xeros_task_start 汇编桩：直接切换到任务
 *     私有栈并调用 xeros_task_wrapper；首次 yield/exit 后，任务上下文
 *     通过 setjmp 建立，后续切换与普通协程一样使用 longjmp。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef XERINTOSH_KERNEL_ESP32_CTX_SWITCH_H
#define XERINTOSH_KERNEL_ESP32_CTX_SWITCH_H

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 原生上下文快照
 * @note 直接使用 newlib 的 jmp_buf（Xtensa windowed ABI，17 个 int）。
 */
typedef jmp_buf kern_ctx_native_t;

/**
 * @brief 保存当前上下文（setjmp 语义）
 * @return 首次调用返回 0，从 xeros_ctx_restore() 恢复时返回 1。
 */
int xeros_ctx_save(kern_ctx_native_t *ctx);

/**
 * @brief 恢复已保存的上下文（longjmp 语义）
 * @note 此函数不返回；执行流直接回到 xeros_ctx_save() 的调用点。
 */
__attribute__((noreturn)) void xeros_ctx_restore(kern_ctx_native_t *ctx);

/**
 * @brief 任务入口包装函数（windowed ABI）
 *
 * 由启动桩调用，负责调用用户任务入口，并在任务自然返回时调用 kern_exit()。
 */
void xeros_task_wrapper(void (*pxCode)(void *), void *pvParameters);

/* 前向声明，避免与 kern_task.h 循环包含 */
struct kern_task;

/**
 * @brief 首次启动一个新任务（不返回）
 *
 * 切换到任务私有栈顶，然后进入 xeros_task_wrapper(entry, arg)。
 * 任务首次 yield/exit 后，其 native_ctx 才会被 setjmp 填充。
 */
__attribute__((noreturn)) void xeros_task_start(struct kern_task *task);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* XERINTOSH_KERNEL_ESP32_CTX_SWITCH_H */
