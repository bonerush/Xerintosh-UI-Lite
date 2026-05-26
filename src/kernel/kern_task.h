/**
 * @file   kern_task.h
 * @brief  Xeros 协作式多任务调度器头文件
 * @details 定义任务控制块（TCB）、动态栈管理、上下文切换、
 *          Round-Robin 调度策略及 sleep/wake 接口。
 *
 *          原生测试使用 ucontext（POSIX 独立栈），
 *          ESP32 使用 setjmp/longjmp（共享栈 + FreeRTOS 隔离）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_TASK_H
#define KERN_TASK_H

#include "kern_types.h"

#ifdef NATIVE_TEST
#include <ucontext.h>
#else
#include <setjmp.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 上下文类型选择 ═══ */

#ifdef NATIVE_TEST
typedef ucontext_t kern_ctx_t;
#else
typedef jmp_buf   kern_ctx_t;
#endif

/* ═══ 任务控制块（TCB） ═══ */

/**
 * @brief 任务控制块
 * @note  动态栈（堆分配），金丝雀溢出检测
 */
typedef struct kern_task {
    kern_pid_t          pid;            /* 任务 ID */
    char                name[KERN_TASK_NAME_LEN + 1];  /* 任务名称 */
    kern_task_state_t   state;          /* 任务状态 */

    /* 动态栈管理 */
    uint8_t            *stack_base;     /* 栈基址（堆分配，native 下为执行栈） */
    size_t              stack_size;     /* 当前栈大小（字节） */
    size_t              stack_used;     /* 峰值栈使用量（字节） */

    /* 上下文保存 */
    kern_ctx_t          ctx;            /* 任务上下文 */
    bool                has_run;        /* 是否已首次运行（区分首次/恢复，仅 setjmp 模式有效） */

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
 * @brief  调度器主循环（从当前任务切换到下一个就绪任务）
 * @note   应在主循环中每帧调用一次
 */
extern void kern_sched_tick(void);

/* ═══ 任务操作 ═══ */

/**
 * @brief  创建并启动新任务
 * @param  name      任务名称（用于调试）
 * @param  entry     任务入口函数
 * @param  arg       入口参数（可为 NULL）
 * @param  stack_min 初始栈大小（0 表示使用 KERN_STACK_MIN）
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

/**
 * @brief  获取当前运行的任务指针
 * @return 当前任务 TCB；无任务时返回 NULL
 */
extern kern_task_t *kern_task_current(void);

/**
 * @brief  获取任务数量（所有状态）
 * @return 任务总数
 */
extern uint8_t kern_task_count(void);

/**
 * @brief  获取指定 PID 的任务
 * @param  pid 任务 ID
 * @return 任务指针；未找到返回 NULL
 */
extern kern_task_t *kern_task_get(kern_pid_t pid);

/**
 * @brief  获取任务队列头（用于遍历）
 * @return 任务链表头指针
 */
extern kern_task_t *kern_task_list_head(void);

/**
 * @brief  估计当前任务的栈使用量
 * @param  task 任务指针
 * @return 栈使用量（字节）；0 表示无法估计
 * @note   通过检查栈底到 SP 的距离计算
 */
extern size_t kern_task_stack_usage(kern_task_t *task);

/**
 * @brief  获取栈金丝雀值
 * @param  task 任务指针
 * @return 金丝雀值（栈底魔数）
 */
extern uint32_t kern_task_stack_canary(kern_task_t *task);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TASK_H */
