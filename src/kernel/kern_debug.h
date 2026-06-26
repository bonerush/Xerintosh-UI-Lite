/**
 * @file   kern_debug.h
 * @brief  Xeros 内核调试与诊断框架
 * @details 提供任务检查、调度追踪、内存画像、IPC 竞争日志。
 *          通过串口 shell 命令和 Python 调试工具访问。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_DEBUG_H
#define KERN_DEBUG_H

#include "kern_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 调度追踪 ═══ */

/** 调度事件类型 */
typedef enum {
    KERN_DBG_SCHED_SWITCH = 0,  /* 任务切换 */
    KERN_DBG_SCHED_YIELD,       /* 主动让出 */
    KERN_DBG_SCHED_SLEEP,       /* 进入睡眠 */
    KERN_DBG_SCHED_WAKE,        /* 被唤醒 */
    KERN_DBG_SCHED_TICK,        /* 调度 tick */
} kern_dbg_sched_event_t;

/** 调度追踪条目 */
typedef struct {
    uint32_t               timestamp;   /* g_sched_ticks */
    kern_dbg_sched_event_t event;       /* 事件类型 */
    kern_pid_t             pid;         /* 相关任务 PID */
    uint8_t                cpu;         /* CPU ID */
} kern_dbg_sched_trace_entry_t;

/**
 * @brief 初始化调度追踪
 * @param buf   追踪缓冲区（调用者分配）
 * @param count 缓冲区条目数
 */
void kern_dbg_sched_trace_init(kern_dbg_sched_trace_entry_t *buf, size_t count);

/**
 * @brief 记录调度事件
 */
void kern_dbg_sched_trace_log(kern_dbg_sched_event_t event, kern_pid_t pid);

/**
 * @brief 遍历调度追踪日志
 * @param cb  回调函数
 * @param ud  用户数据
 */
void kern_dbg_sched_trace_dump(void (*cb)(const kern_dbg_sched_trace_entry_t *entry, void *ud), void *ud);

/* ═══ 任务检查 ═══ */

/** 任务状态快照 */
typedef struct {
    kern_pid_t          pid;
    char                name[16];
    kern_task_state_t   state;
    uint8_t             priority;
    uint8_t             cpu_id;
    size_t              stack_size;
    size_t              stack_used;
    uint32_t            wake_time;
} kern_dbg_task_info_t;

/**
 * @brief 获取指定任务的状态快照
 * @param pid 任务 PID
 * @param out 输出结构体
 * @return true 成功，false 任务不存在
 */
bool kern_dbg_task_info(kern_pid_t pid, kern_dbg_task_info_t *out);

/**
 * @brief 遍历所有任务并调用回调
 */
void kern_dbg_task_dump(void (*cb)(const kern_dbg_task_info_t *info, void *ud), void *ud);

/* ═══ 内存画像 ═══ */

/** 内存统计快照 */
typedef struct {
    size_t total_heap;
    size_t free_heap;
    size_t min_free_heap;
    size_t allocated;
    uint8_t pressure_level;  /* 0=低, 1=中, 2=高 */
} kern_dbg_mem_info_t;

/**
 * @brief 获取内存统计
 */
void kern_dbg_mem_info(kern_dbg_mem_info_t *out);

/* ═══ IPC 竞争日志 ═══ */

/** IPC 竞争事件 */
typedef struct {
    uint32_t  timestamp;
    uint8_t   type;       /* 0=sem, 1=mutex, 2=queue, 3=event */
    kern_pid_t waiter;    /* 等待者 PID */
    kern_pid_t holder;    /* 持有者 PID（mutex 用） */
    uint32_t  wait_ms;    /* 等待时长 */
} kern_dbg_ipc_event_t;

/**
 * @brief 记录 IPC 竞争事件
 */
void kern_dbg_ipc_log(uint8_t type, kern_pid_t waiter, kern_pid_t holder, uint32_t wait_ms);

/**
 * @brief 遍历 IPC 竞争日志
 */
void kern_dbg_ipc_dump(void (*cb)(const kern_dbg_ipc_event_t *entry, void *ud), void *ud);

/* ═══ Shell 命令注册 ═══ */

/**
 * @brief 注册调试相关的 shell 命令
 * @note  在 kern_init 中调用
 */
void kern_debug_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_DEBUG_H */
