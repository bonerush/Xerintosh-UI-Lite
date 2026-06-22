/**
 * @file   kern_ctx_esp32.h
 * @brief  Xtensa (ESP32) setjmp/longjmp 上下文切换原语
 *
 * @details 为 XEROS_NATIVE_SCHED 模式提供协程上下文切换。
 *
 *          ⚠️ 已知限制（2026-05）:
 *          - Xtensa newlib 的 setjmp 从当前 SP 读取 entry 保存区
 *            ("4 words from the save area at proc's $sp")。
 *          - 在任务栈上调用 setjmp（切换 SP 后）读取到的是未初始化的
 *            0xAA，导致 longjmp 恢复时 StoreProhibited 崩溃。
 *          - 共享栈方案（所有任务共用 loop() 栈）因 init 代码返回后
 *            栈帧被复用，longjmp 恢复后任务与调度器帧冲突导致 WDT。
 *
 *          当前项目使用 FreeRTOS 双信号量包装器（kern_port.c），
 *          FreeRTOS 依赖已隔离到单一文件。此头文件保留供未来参考。
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

typedef struct {
    jmp_buf  jmp;
    void    *sp;
} kern_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_TEST */
#endif /* KERN_CTX_ESP32_H */
