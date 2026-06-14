/**
 * @file   kern_smp.h
 * @brief  Xeros SMP 多核支持头文件
 * @details 定义 per-CPU 数据结构、CPU ID 查询、核心管理接口。
 *          当 CONFIG_SMP_ENABLED 未定义时，通过宏将 per-CPU 变量映射回
 *          g_per_cpu[0]，实现零开销向后兼容。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SMP_H
#define KERN_SMP_H

#include "kern_types.h"

/* 前向声明（避免循环依赖 kern_task.h ↔ kern_smp.h） */
struct kern_task;

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define KERN_MAX_CPUS  2     /* ESP32: PRO_CPU=0, APP_CPU=1 */
#define KERN_CPU_ANY   0xFF  /* 自动分配到负载最低的 CPU */

/* ═══ Per-CPU 数据结构 ═══ */

typedef struct kern_per_cpu {
    uint8_t       cpu_id;          /* CPU 编号 */
    struct kern_task  *current_task;    /* 当前运行的任务 */
    struct kern_task  *idle_task;       /* 此 CPU 的 idle 任务 */
    uint32_t      sched_ticks;     /* 调度 tick 计数 */
    volatile bool need_resched;    /* 抢占请求标志（可被 ISR 设置） */
    struct kern_task  *last_picked;     /* RR 扫描起点（每核独立） */
    uint8_t       task_count;      /* 此 CPU 管理的任务数 */
} kern_per_cpu_t;

/** 全局 per-CPU 数组（总是存在，SMP 禁用时仅使用 [0]） */
extern kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];

/* ═══ CPU 标识 ═══ */

#ifdef CONFIG_SMP_ENABLED

#define KERN_THIS_CPU  ((uint8_t)(kern_cpu_id()))

/** 返回当前 CPU ID（ESP32: xPortGetCoreID，native: 0） */
uint8_t kern_cpu_id(void);

/** 初始化 SMP 子系统（per-CPU 数组、APP_CPU 调度器） */
void kern_smp_init(void);

/** 在指定 CPU 上启动调度器循环 */
void kern_smp_start_core(uint8_t cpu_id, void (*entry)(void *arg));

/** 为 KERN_CPU_ANY 任务分配 CPU（负载最低的核心） */
uint8_t kern_smp_migrate_assign(void);

#else /* !CONFIG_SMP_ENABLED — 单核零开销模式 */

#define KERN_THIS_CPU  ((uint8_t)0)

static inline uint8_t kern_cpu_id(void) { return 0; }
#define kern_smp_init()                 do {} while (0)
#define kern_smp_start_core(c, e)       do { (void)(c); (void)(e); } while (0)
static inline uint8_t kern_smp_migrate_assign(void) { return 0; }

#endif /* CONFIG_SMP_ENABLED */

/* ═══ Per-CPU 访问宏（零开销：编译器将 g_per_cpu[0] 优化为直接访问） ═══ */

#define g_current_task   (g_per_cpu[KERN_THIS_CPU].current_task)
#define g_idle_task      (g_per_cpu[KERN_THIS_CPU].idle_task)
#define g_sched_ticks    (g_per_cpu[KERN_THIS_CPU].sched_ticks)
#define g_last_picked    (g_per_cpu[KERN_THIS_CPU].last_picked)
#define g_percpu_task_count  (g_per_cpu[KERN_THIS_CPU].task_count)

/* 无条件定义：kern_sched.c 在任何配置下都使用此宏 */
#define g_need_resched   (g_per_cpu[KERN_THIS_CPU].need_resched)

#ifdef __cplusplus
}
#endif

#endif /* KERN_SMP_H */
