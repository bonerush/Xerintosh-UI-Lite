/**
 * @file   kern_sched_class.h
 * @brief  Xeros 可插拔调度器类接口
 * @details 定义 kern_sched_class_t 结构体，允许注册多种调度策略
 *          （Round-Robin、优先级 FIFO 等），调度器按优先级顺序遍历各 class
 *          的 pick_next() 选择任务。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SCHED_CLASS_H
#define KERN_SCHED_CLASS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
struct kern_task;

/* ═══ 调度器类接口 ═══ */

/**
 * @brief 调度器类操作表
 * @note  每个调度策略实现此接口，编译时通过 kern_sched_class_register()
 *        注册到全局 class 数组，按注册顺序确定优先级（先注册 = 高优先级）。
 */
typedef struct kern_sched_class {
    const char *name;                               /* 类名称（调试用） */

    void (*enqueue)(struct kern_task *task);        /* 将任务加入本 class */
    void (*dequeue)(struct kern_task *task);        /* 将任务从本 class 移除 */
    struct kern_task *(*pick_next)(void);           /* 从本 class 中选择下一个任务 */
    void (*tick)(struct kern_task *current);        /* 定时器 tick：时间片递减 / 抢占检测 */
    void (*prio_changed)(struct kern_task *task,
                         uint8_t old_prio);         /* 任务优先级变更通知 */

    struct kern_task *task_list;                    /* 本 class 的任务链表头 */
} kern_sched_class_t;

/* ═══ 全局 class 注册表 ═══ */

/** 全局调度器 class 数组（按优先级顺序，NULL 终止） */
extern kern_sched_class_t *g_sched_classes[];

/** 当前注册的 class 数量 */
extern uint8_t g_sched_class_count;

/** 最大可注册的 class 数量 */
#define KERN_SCHED_MAX_CLASSES 8

/**
 * @brief  注册一个调度器 class
 * @param  cls 类实例指针
 * @return KERN_OK 成功，KERN_EINVAL 参数无效，KERN_ENOSPC 注册表已满
 * @note   按注册顺序决定 class 优先级（先注册 = 先被 pick_next_ready 查询）
 *         必须在 kern_sched_init() 中调用。
 */
kern_err_t kern_sched_class_register(kern_sched_class_t *cls);

/**
 * @brief  从所有 class 列表中选择下一个就绪任务
 * @return 下一个应运行的任务，无就绪任务时返回 NULL
 * @note   按 class 注册顺序遍历，返回第一个非 NULL 结果。
 *         替代原 kern_sched.c 中的 pick_next_ready()。
 */
struct kern_task *pick_next_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_SCHED_CLASS_H */
