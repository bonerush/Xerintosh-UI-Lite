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

#ifdef NATIVE_TEST
#include <ucontext.h>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 上下文类型选择 ═══ */

#ifdef NATIVE_TEST
typedef ucontext_t    kern_ctx_t;
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
    uint8_t            *stack_base;     /* 栈底指针（Native 堆分配；ESP32 未使用） */
    size_t              stack_size;     /* 栈大小（字节） */

    /* 上下文保存 */
#ifdef NATIVE_TEST
    kern_ctx_t          ctx;            /* Native: ucontext_t */
#endif
    bool                has_run;        /* 是否已首次运行 */
#ifndef NATIVE_TEST
    TaskHandle_t        rtos_handle;    /* FreeRTOS 任务句柄 */
#endif

    /* 调度信息 */
    uint32_t            wake_time;      /* 唤醒时间戳（毫秒，sleep 用） */
    uint8_t             priority;       /* 优先级（0 最低，255 最高） */

    /* 入口 */
    void              (*entry)(void *arg);  /* 任务主函数 */
    void               *arg;            /* 入口参数 */

    /* 链表指针 */
    struct kern_task   *next;           /* 下一个任务（就绪/睡眠队列） */
} kern_task_t;

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
 * @brief  休眠指定的毫秒数
 * @param  ms 休眠时间（毫秒）
 * @note   当前任务进入 SLEEPING 状态，ms 毫秒后被唤醒
 */
extern void kern_sleep_ms(uint32_t ms);

/* ═══ 查询接口 ═══ */

extern kern_task_t *kern_task_current(void);
extern uint8_t kern_task_count(void);
extern kern_task_t *kern_task_get(kern_pid_t pid);
extern kern_task_t *kern_task_list_head(void);
extern size_t kern_task_stack_usage(kern_task_t *task);
extern uint32_t kern_task_stack_canary(kern_task_t *task);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TASK_H */
