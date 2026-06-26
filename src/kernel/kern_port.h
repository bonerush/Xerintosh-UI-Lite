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
 *          所有与 FreeRTOS 的直接交互仅限于 kern_port_freertos.c。
 *          内核其他模块（kern_task 等）仅通过本接口调用。
 *
 *          自 kernel-v2-phase1 起，公共 API 通过 kern_port_ops_t 结构体
 *          实现后端多态。kern_port_*() 函数变为 static inline 包装器，
 *          直接调用 g_kern_port_ops 中的函数指针。
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
#else
  /* ESP32 + FreeRTOS 任务容器（默认与 XEROS_NATIVE_SCHED fallback） */
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

/* ═══ 可移植层操作表（多态后端） ═══ */

/**
 * @brief 可移植层操作表
 * @note  每个后端（FreeRTOS / 原生 / 测试桩）提供自己的实现，
 *        编译时通过 #if 守卫选择，运行时通过此表分派。
 */
typedef struct kern_port_ops {
    /* 生命周期 */
    void  (*init)(void);

    /* 线程管理 */
    kern_port_thread_t (*thread_spawn)(
        void (*entry)(void*), void *arg, const char *name,
        size_t stack_size, struct kern_task *task);
    void  (*thread_exit)(void);
    void  (*thread_kill)(kern_port_thread_t);
    size_t (*thread_stack_usage)(kern_port_thread_t);

    /* 上下文切换 */
    void  (*switch_to)(struct kern_task *task);
    void  (*task_yield)(void);
    void  (*task_exit)(void);

    /* 空闲处理 */
    void  (*idle)(void);

    /* 定时器基础设施（抢占式调度用） */
    int   (*timer_set_periodic)(uint32_t period_us);
    void  (*timer_stop)(void);
    bool  (*preempt_consume)(void);  /* 检查并消费 ISR 抢占 tick 请求 */
} kern_port_ops_t;

/**
 * @brief 全局操作表实例（由当前激活的后端定义）
 */
extern const kern_port_ops_t g_kern_port_ops;

/* ═══ 公共 API（static inline 包装器） ═══ */

/**
 * @brief 初始化可移植层
 * @note  创建调度所需的基础设施（信号量、上下文等）
 */
static inline void kern_port_init(void)
{
    g_kern_port_ops.init();
}

/**
 * @brief  创建新的执行上下文
 * @param  entry      入口函数
 * @param  arg        入口参数
 * @param  name       线程名（调试用）
 * @param  stack_size 栈大小（**字节**，0 表示使用默认值）
 * @param  task       关联的 Xeros TCB 指针
 * @return 线程句柄，失败返回 KERN_PORT_THREAD_NULL
 * @note   FreeRTOS 后端：stack_size 在创建时转换为 StackType_t 字数后传入
 *         xTaskCreatePinnedToCore。创建后 Xeros 无法调整该栈大小或回收。
 *         如需“按需分配”，请在创建前使用 kern_task_stack_recommend()。
 */
static inline kern_port_thread_t kern_port_thread_spawn(
    void (*entry)(void *arg),
    void *arg,
    const char *name,
    size_t stack_size,
    struct kern_task *task)
{
    return g_kern_port_ops.thread_spawn(entry, arg, name, stack_size, task);
}

/**
 * @brief  退出当前线程（不返回）
 * @note   将当前线程标记为退出并归还 CPU。
 *         调用后不会返回——线程将被底层销毁。
 */
static inline void kern_port_thread_exit(void)
{
    g_kern_port_ops.thread_exit();
    __builtin_unreachable();
}

/**
 * @brief  从外部销毁指定线程
 * @param  thread 线程句柄
 * @note   用于 kern_task_kill() 等外部终止场景。
 *         被销毁的线程必须不在当前 CPU 上执行。
 */
static inline void kern_port_thread_kill(kern_port_thread_t thread)
{
    g_kern_port_ops.thread_kill(thread);
}

/**
 * @brief  获取线程栈使用高水位
 * @param  thread 线程句柄
 * @return 剩余栈字数（FreeRTOS 后端）或已使用栈字节数（Native 后端）。
 *         调用者 kern_task_stack_usage() 负责按后端统一转换为字节。
 */
static inline size_t kern_port_thread_stack_usage(kern_port_thread_t thread)
{
    return g_kern_port_ops.thread_stack_usage(thread);
}

/* ═══ 上下文切换 ═══ */

/**
 * @brief  从调度器切换到目标任务（阻塞调用）
 * @param  task 目标 Xeros 任务
 * @note   调度器侧调用：将 CPU 令牌交给 task，阻塞等待 task 归还。
 *         当 task 调用 kern_port_task_yield() 或 kern_port_task_exit() 时返回。
 */
static inline void kern_port_switch_to(struct kern_task *task)
{
    g_kern_port_ops.switch_to(task);
}

/**
 * @brief  从当前任务切换回调度器（yield）
 * @note   任务侧调用：保存上下文，归还 CPU 令牌，阻塞等待下次被调度。
 */
static inline void kern_port_task_yield(void)
{
    g_kern_port_ops.task_yield();
}

/**
 * @brief  从当前任务退出并归还 CPU（不返回）
 * @note   任务侧调用：归还 CPU 令牌并销毁当前线程。
 */
static inline void kern_port_task_exit(void)
{
    g_kern_port_ops.task_exit();
    __builtin_unreachable();
}

/* ═══ 空闲处理 ═══ */

/**
 * @brief  调度器空闲时的处理（无可运行任务）
 * @note   短暂让出 CPU 让底层处理系统事务（WiFi/BT 等）
 */
static inline void kern_port_idle(void)
{
    g_kern_port_ops.idle();
}

/* ═══ 定时器基础设施 ═══ */

/**
 * @brief  启动周期性硬件定时器（抢占式调度 ISR 源）
 * @param  period_us 周期（微秒）
 * @return 0 成功，< 0 失败
 * @note   ISR 仅设置内部抢占标志，不执行调度逻辑。
 *         实际调度由 loop() 中 kern_port_preempt_consume() 触发。
 */
static inline int kern_port_timer_set_periodic(uint32_t period_us)
{
    return g_kern_port_ops.timer_set_periodic(period_us);
}

/**
 * @brief  停止硬件定时器
 */
static inline void kern_port_timer_stop(void)
{
    g_kern_port_ops.timer_stop();
}

/**
 * @brief  检查并消费硬件定时器 ISR 的抢占 tick 请求
 * @return true 如果有待处理的抢占 tick（消费后清零）
 * @note   在 loop() 任务上下文中调用。当抢占 tick 产生时，
 *         loop() 应立即调用 kern_sched_tick() 执行一次调度。
 */
static inline bool kern_port_preempt_consume(void)
{
    return g_kern_port_ops.preempt_consume();
}

#ifdef __cplusplus
}
#endif

#endif /* KERN_PORT_H */
