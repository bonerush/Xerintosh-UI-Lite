/**
 * @file   kern_port.h
 * @brief  Xeros 内核可移植层头文件
 * @details 抽象底层执行上下文（线程创建、上下文切换、栈查询），
 *          将 FreeRTOS / setjmp-longjmp 等具体实现隔离在端口层。
 *
 *          当前支持两种后端：
 *          - FreeRTOS 任务容器（默认，`#else` 路径）
 *          - 原生 setjmp/longjmp（`#ifdef XEROS_NATIVE_SCHED`，实验性）
 *
 *          所有与 FreeRTOS 的直接交互仅限于 kern_port.c。
 *          内核其他模块（kern_task 等）仅通过本接口调用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_PORT_H
#define KERN_PORT_H

#include "kern_types.h"

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 平台选择 ═══ */

/* 前向声明 kern_task_t（避免循环依赖） */
struct kern_task;

#ifdef NATIVE_TEST
  /* Native: 使用 POSIX ucontext（无 FreeRTOS） */
  #define KERN_PORT_STACK_MIN  1024
#elif defined(XEROS_NATIVE_SCHED)
  /* ESP32 原生调度器：setjmp/longjmp + 手动栈管理 */
  #include "kern_ctx_esp32.h"
  #define KERN_PORT_STACK_MIN  4096
#else
  /* ESP32 + FreeRTOS 任务容器（默认） */
  #define KERN_PORT_STACK_MIN  4096
#endif

/* ═══ 不透明线程句柄 ═══ */

/**
 * @brief 底层执行上下文句柄
 * @note  FreeRTOS 后端为 TaskHandle_t
 *        原生后端为 NULL（栈由 Xeros 管理）
 */
typedef void* kern_port_thread_t;

#define KERN_PORT_THREAD_NULL  ((kern_port_thread_t)0)

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化可移植层
 * @note  创建调度所需的基础设施（信号量、上下文等）
 */
void kern_port_init(void);

/* ═══ 线程管理 ═══ */

/**
 * @brief  创建新的执行上下文
 * @param  entry      入口函数
 * @param  arg        入口参数
 * @param  name       线程名（调试用）
 * @param  stack_size 栈大小（字节，0 表示使用默认值）
 * @param  task       关联的 Xeros TCB 指针
 * @return 线程句柄，失败返回 KERN_PORT_THREAD_NULL
 */
kern_port_thread_t kern_port_thread_spawn(
    void (*entry)(void *arg),
    void *arg,
    const char *name,
    size_t stack_size,
    struct kern_task *task
);

/**
 * @brief  退出当前线程（不返回）
 * @note   将当前线程标记为退出并归还 CPU。
 *         调用后不会返回——线程将被底层销毁。
 */
void kern_port_thread_exit(void) __attribute__((noreturn));

/**
 * @brief  从外部销毁指定线程
 * @param  thread 线程句柄
 * @note   用于 kern_task_kill() 等外部终止场景。
 *         被销毁的线程必须不在当前 CPU 上执行。
 */
void kern_port_thread_kill(kern_port_thread_t thread);

/**
 * @brief  获取线程栈使用高水位
 * @param  thread 线程句柄
 * @return 已使用的栈字节数（近似值）
 */
size_t kern_port_thread_stack_usage(kern_port_thread_t thread);

/* ═══ 上下文切换 ═══ */

/**
 * @brief  从调度器切换到目标任务（阻塞调用）
 * @param  task 目标 Xeros 任务
 * @note   调度器侧调用：将 CPU 令牌交给 task，阻塞等待 task 归还。
 *         当 task 调用 kern_port_task_yield() 或 kern_port_task_exit() 时返回。
 */
void kern_port_switch_to(struct kern_task *task);

/**
 * @brief  从当前任务切换回调度器（yield）
 * @note   任务侧调用：保存上下文，归还 CPU 令牌，阻塞等待下次被调度。
 */
void kern_port_task_yield(void);

/**
 * @brief  从当前任务退出并归还 CPU（不返回）
 * @note   任务侧调用：归还 CPU 令牌并销毁当前线程。
 */
void kern_port_task_exit(void) __attribute__((noreturn));

/* ═══ 空闲处理 ═══ */

/**
 * @brief  调度器空闲时的处理（无可运行任务）
 * @note   短暂让出 CPU 让底层处理系统事务（WiFi/BT 等）
 */
void kern_port_idle(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_PORT_H */
