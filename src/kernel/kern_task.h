/**
 * @file   kern_task.h
 * @brief  Xeros 协作式多任务调度器头文件
 * @details 定义任务控制块（TCB）、动态栈管理、上下文切换、
 *          Round-Robin 调度策略及 sleep/wake 接口。
 *
 *          Native 测试：使用 ucontext（POSIX 独立栈）
 *          ESP32：使用 FreeRTOS 任务承载（每任务独立栈），
 *                 互斥锁实现协作式调度
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_TASK_H
#define KERN_TASK_H

#include "kern_types.h"
#include "kern_port.h"

#ifdef NATIVE_TEST
#include <ucontext.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 上下文类型选择 ═══ */

#ifdef NATIVE_TEST
typedef ucontext_t    kern_ctx_t;
#elif defined(XEROS_NATIVE_SCHED)
#include "kern_ctx_esp32.h"
#endif

/* ═══ 任务控制块（TCB） ═══ */

/**
 * @brief 任务控制块
 * @note  Native: 动态栈（堆分配），ucontext 上下文
 *        ESP32: FreeRTOS 任务承载，独立栈由 FreeRTOS 管理
 */
typedef struct kern_task {
    kern_pid_t          pid;            /* 任务 ID */
    char                name[KERN_TASK_NAME_LEN + 1];  /* 任务名称 */
    kern_task_state_t   state;          /* 任务状态 */

    /* 动态栈管理（Native: 堆分配+ucontext；ESP32: FreeRTOS 管理栈） */
    uint8_t            *stack_base;     /* 栈底指针 */
    size_t              stack_size;     /* 栈大小（字节） */

    /* 上下文保存 */
#if defined(NATIVE_TEST)
    kern_ctx_t          ctx;            /* ucontext */
#elif defined(XEROS_NATIVE_SCHED)
    kern_ctx_t          ctx;            /* setjmp/longjmp */
#else
    kern_port_thread_t  port_thread;    /* 底层执行上下文句柄 */
#endif

    /* 调度信息 */
    uint32_t            wake_time;      /* 唤醒时间戳（毫秒，sleep 用） */
    uint8_t             priority;       /* 优先级（0 最低，255 最高） */

    /* 入口 */
    void              (*entry)(void *arg);  /* 任务主函数 */
    void               *arg;            /* 入口参数 */

    /* 标志位 */
    uint8_t             flags;          /* KERN_TASK_FLAG_* */

    /* 链表指针 */
    struct kern_task   *next;           /* 下一个任务（就绪/睡眠队列） */
} kern_task_t;

/* ═══ 任务标志位 ═══ */

#define KERN_TASK_FLAG_VIRTUAL  0x01   /* 虚任务：无独立上下文，不参与调度 */

/* ═══ 调度器生命周期 ═══ */

/**
 * @brief 初始化任务调度器
 * @note  创建 idle 任务，初始化就绪队列
 */
extern void kern_sched_init(void);

/**
 * @brief  调度器主循环
 * @note   Native: 切换到下一个就绪任务
 *         ESP32: 释放互斥锁给下一个任务，等待锁归还
 */
extern void kern_sched_tick(void);

/* ═══ 任务操作 ═══ */

/**
 * @brief  创建并启动新任务
 * @param  name      任务名称（用于调试）
 * @param  entry     任务入口函数
 * @param  arg       入口参数（可为 NULL）
 * @param  stack_min 初始栈大小（0 表示使用默认值）
 * @return PID >= 0 成功，< 0 为错误码
 */
extern kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                              void *arg, size_t stack_min);

/**
 * @brief  主动让出 CPU
 * @note   当前任务状态保持 READY，调度器选择下一个任务运行
 */
extern void kern_yield(void);

/**
 * @brief  终止当前任务
 * @note   当前任务变为 ZOMBIE，不立即释放资源（需父任务回收）
 */
extern void kern_exit(void);

/**
 * @brief  从外部终止指定任务
 * @param  pid 要终止的任务 PID
 * @return 0 成功，< 0 为错误码
 * @note   受保护的系统任务不可终止（返回 KERN_EACCES）。
 *         当前任务不可终止自身（返回 KERN_EACCES）。
 *         虚任务：调用 kern_task_unregister_virtual() 注销。
 *         非虚任务：标记 ZOMBIE 并销毁底层 FreeRTOS 线程。
 */
extern int kern_task_kill(kern_pid_t pid);

/**
 * @brief  休眠指定的毫秒数
 * @param  ms 休眠时间（毫秒）
 * @note   当前任务进入 SLEEPING 状态，ms 毫秒后被唤醒
 */
extern void kern_sleep_ms(uint32_t ms);

/* ═══ 工具函数 ═══ */

/**
 * @brief  通用轮询任务循环（永不返回）
 * @param  update_fn   每轮调用的更新函数
 * @param  interval_ms 轮询间隔（毫秒）
 * @note   替代手写的 for(;;) { update(); kern_sleep_ms(n); } 模式
 */
static inline void kern_poll_loop(void (*update_fn)(void), uint32_t interval_ms)
{
    for (;;) {
        update_fn();
        kern_sleep_ms(interval_ms);
    }
}

/* ═══ 查询接口 ═══ */

extern kern_task_t *kern_task_current(void);
extern kern_task_t *kern_task_get(kern_pid_t pid);
extern kern_task_t *kern_task_list_head(void);
extern size_t kern_task_stack_usage(kern_task_t *task);

/* ═══ 虚任务管理 ═══ */

/**
 * @brief  注册虚任务到内核任务链表
 * @param  name 任务名称（用于 /proc/tasks 显示）
 * @return PID >= 0 成功，< 0 为错误码
 * @note   虚任务不创建 FreeRTOS 上下文，不参与调度。
 *         仅用于内核可观测性（/proc/tasks 可见、kill 可终止）。
 *         当 App 退出时必须调用 kern_task_unregister_virtual() 注销。
 */
extern kern_pid_t kern_task_register_virtual(const char *name);

/**
 * @brief  注销虚任务
 * @param  pid 要注销的虚任务 PID
 * @note   将任务标记为 ZOMBIE，并回收 TCB 内存。
 *         仅对虚任务有效。
 */
extern void kern_task_unregister_virtual(kern_pid_t pid);

/**
 * @brief  检查任务是否为受保护的系统任务
 * @param  task 任务指针
 * @return true  系统关键任务，不可终止
 * @return false 普通任务，可以终止
 */
extern bool kern_task_is_protected(kern_task_t *task);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TASK_H */
