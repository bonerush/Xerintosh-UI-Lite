/**
 * @file   kern_port_native.c
 * @brief  Xeros 内核可移植层 — 原生 setjmp/longjmp 后端（实验性）
 * @details 基于 setjmp/longjmp + 手动栈管理实现协作式调度，
 *          完全不依赖 FreeRTOS 任务 API。
 *
 * @deprecated 此文件具有互斥的 #ifdef 守卫（XEROS_NATIVE_SCHED），
 *             在当前所有构建配置中均不参与编译。保留作为实验性参考代码。
 *             不要在新代码中包含或引用此文件。
 *
 * @note   当 XEROS_NATIVE_SCHED 启用时，调度逻辑直接在 kern_task.c 中实现，
 *         此文件不参与编译。
 *
 * @note   kernel-v2-phase1 引入了 kern_port_ops_t 多态模式：
 *         如果将来激活此后端，应将各 kern_port_* 函数重命名为静态函数
 *         （例如 kern_port_native_init），并定义 g_kern_port_ops 操作表。
 *         当前此文件不编译，因此无需改动。
 */

#ifndef XEROS_NATIVE_SCHED

/*
 * ⚠️  实验性：默认不编译。
 * 需同时定义 NATIVE_TEST=0 和 XEROS_NATIVE_SCHED 才激活。
 * 在 platformio.ini 中添加 -DXEROS_NATIVE_SCHED 编译选项。
 *
 * 使用时将此文件替换 kern_port.c（FreeRTOS 后端）。
 *
 * @copyright Copyright (c) 2026
 */

#if !defined(NATIVE_TEST) && defined(XEROS_NATIVE_SCHED)

#include "kern_port.h"
#include "kern_task.h"
#include "kern_init.h"
#include "kern_ctx_esp32.h"

#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

/* ═══ 内部常量 ═══ */

#define NATIVE_STACK_MIN  4096   /* 每任务最小栈 */
#define NATIVE_STACK_MAX  8192   /* 每任务最大栈 */

/* ═══ 调度器上下文 ═══ */

static kern_ctx_t g_sched_ctx;          /* 调度器自身的上下文 */
static bool       g_port_initialized = false;

/* ═══ 线程管理 ═══ */

/* 原生模式下不使用不透明句柄，所有状态在 TCB 的 kern_ctx_t 中 */
typedef struct {
    uint8_t *stack_base;     /* 栈底（用于 free） */
    size_t   stack_size;     /* 栈大小 */
    kern_ctx_t ctx;          /* setjmp/longjmp 上下文 */
} native_thread_t;

void kern_port_init(void)
{
    if (g_port_initialized) return;
    g_port_initialized = true;

    /* 调度器上下文由 kern_task.c 的 g_current_task 管理，
     * 此处仅标记端口已初始化 */
    kern_log(KERN_LOG_INFO, "port: native scheduler (setjmp/longjmp) initialized");
}

kern_port_thread_t kern_port_thread_spawn(
    void (*entry)(void *arg),
    void *arg,
    const char *name,
    size_t stack_size,
    kern_task_t *task)
{
    (void)entry;     /* entry 从 task->entry 读取 */
    (void)name;      /* 仅调试用 */

    if (task == NULL || task->entry == NULL) {
        return KERN_PORT_THREAD_NULL;
    }

    if (stack_size < NATIVE_STACK_MIN) stack_size = NATIVE_STACK_MIN;
    if (stack_size > NATIVE_STACK_MAX) stack_size = NATIVE_STACK_MAX;

    /* 分配原生线程结构 */
    native_thread_t *nt = (native_thread_t *)calloc(1, sizeof(native_thread_t));
    if (nt == NULL) return KERN_PORT_THREAD_NULL;

    /* 分配任务栈 */
    nt->stack_base = (uint8_t *)malloc(stack_size);
    if (nt->stack_base == NULL) {
        free(nt);
        return KERN_PORT_THREAD_NULL;
    }
    nt->stack_size = stack_size;

    /* 写入金丝雀 */
    if (stack_size >= sizeof(uint32_t)) {
        uint32_t canary = 0xDEADC0DE;
        memcpy(nt->stack_base, &canary, sizeof(uint32_t));
    }
    memset(nt->stack_base + sizeof(uint32_t), 0xAA, stack_size - sizeof(uint32_t));

    /* 初始化上下文：在任务栈上调用 setjmp */
    uint8_t *stack_top = nt->stack_base + stack_size - 16;  /* 16 字节对齐 */
    kern_ctx_init(&nt->ctx, nt->stack_base, stack_top, task->entry, task->arg);

    /* 将原生线程指针存入 TCB 的 ctx 字段（复用） */
    task->stack_base = nt->stack_base;
    task->stack_size = nt->stack_size;

    return (kern_port_thread_t)nt;
}

void kern_port_thread_exit(void)
{
    /* 释放当前任务资源，跳转回调度器 */
    kern_task_t *cur = NULL;
    /* cur = kern_task_current() 需要从 kern_task.c 获取 */
    /* 简化：直接跳转回调度器上下文 */
    longjmp(g_sched_ctx.jmp, 1);

    /* 不会到达 */
    while (1) {}
}

size_t kern_port_thread_stack_usage(kern_port_thread_t thread)
{
    if (thread == KERN_PORT_THREAD_NULL) return 0;

    native_thread_t *nt = (native_thread_t *)thread;
    if (nt->stack_base == NULL) return 0;

    /* 从栈底向上扫描，找到第一个非 0xAA 字节 */
    size_t used = 0;
    for (size_t i = sizeof(uint32_t); i < nt->stack_size; i++) {
        if (nt->stack_base[i] != 0xAA) {
            used = nt->stack_size - i;
            break;
        }
    }
    return used;
}

/* ═══ 上下文切换 ═══ */

void kern_port_switch_to(kern_task_t *task)
{
    if (task == NULL || task->port_thread == KERN_PORT_THREAD_NULL) return;

    native_thread_t *nt = (native_thread_t *)task->port_thread;

    /*
     * 保存调度器上下文并跳转到目标任务：
     *   if (setjmp(sched_ctx) == 0)
     *       longjmp(task_ctx, 1)
     *
     * 当任务 yield 时，会 longjmp 回 sched_ctx，setjmp 返回非 0。
     */
    if (setjmp(g_sched_ctx.jmp) == 0) {
        longjmp(nt->ctx.jmp, 1);
    }
    /* 任务已 yield/exit：控制权回到调度器 */
}

void kern_port_task_yield(void)
{
    /*
     * 保存任务上下文并跳转回调度器：
     *   从 kern_port_switch_to 的 setjmp 处返回（返回值为 1）
     */
    /* 上下文已由 kern_ctx_esp32.h 的 kern_ctx_init 中的 setjmp 保存。
     * 直接 longjmp 回调度器即可。 */
    longjmp(g_sched_ctx.jmp, 1);
}

void kern_port_task_exit(void)
{
    /* 跳转回调度器（不保存任务上下文，因为任务即将销毁） */
    longjmp(g_sched_ctx.jmp, 1);

    /* 不会到达 */
    while (1) {}
}

/* ═══ 空闲处理 ═══ */

void kern_port_idle(void)
{
    /*
     * 原生模式下无可运行任务时，短暂延时让出 CPU。
     * 使用 esp_timer 而非 vTaskDelay（避开 FreeRTOS 依赖）。
     */
    /* TODO: 使用 esp_timer 或简单的忙等待 */
    /* 当前简化：短暂的忙等待循环 */
    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }
}

#endif /* !NATIVE_TEST && XEROS_NATIVE_SCHED */

#endif /* !XEROS_NATIVE_SCHED — 文件在 XEROS_NATIVE_SCHED 下不参与编译 */
