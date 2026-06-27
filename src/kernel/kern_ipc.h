/**
 * @file   kern_ipc.h
 * @brief  Xeros IPC 原语头文件
 * @details 定义二值信号量、计数信号量、消息队列、事件组。
 *          所有原语支持阻塞等待和超时机制。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_IPC_H
#define KERN_IPC_H

#include "kern_types.h"
#include "kern_task.h"
#include "kern_sync.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 等待队列节点 ═══ */

/**
 * @brief IPC 等待队列节点（嵌入 TCB 或独立分配）
 */
typedef struct kern_wait_node {
    kern_task_t           *task;     /* 等待的任务 */
    struct kern_wait_node *next;     /* 链表下一节点 */
    uint32_t               timeout;  /* 唤醒时间戳（ms），0 = 永不超时 */
} kern_wait_node_t;

/* ═══ 二值信号量 ═══ */

/**
 * @brief 二值信号量
 *
 * 状态：0（空）或 1（有令牌）。
 * give: 状态 0→1，唤醒一个等待者。
 * take: 状态 1→0 立即成功；状态 0 则阻塞。
 */
typedef struct {
    volatile int32_t   count;       /* 0 或 1 */
    xeros_spinlock_t   lock;        /* SMP 保护 */
    kern_wait_node_t  *wait_queue;  /* 等待队列 */
} kern_bin_sem_t;

#define KERN_BIN_SEM_INIT(name)  { .count = 0, .lock = { .locked = false }, .wait_queue = NULL }

static inline void kern_bin_sem_init(kern_bin_sem_t *sem, int32_t initial)
{
    sem->count = (initial != 0) ? 1 : 0;
    xeros_spinlock_init(&sem->lock);
    sem->wait_queue = NULL;
}

extern kern_err_t kern_bin_sem_give(kern_bin_sem_t *sem);
extern kern_err_t kern_bin_sem_take(kern_bin_sem_t *sem, uint32_t timeout_ms);

/* ═══ 计数信号量 ═══ */

/**
 * @brief 计数信号量
 *
 * count 范围：[0, max_count]。
 * give: count++，唤醒一个等待者。
 * take: count>0 则 count-- 立即成功；count==0 则阻塞。
 */
typedef struct {
    volatile int32_t   count;
    int32_t            max_count;
    xeros_spinlock_t   lock;        /* SMP 保护 */
    kern_wait_node_t  *wait_queue;
} kern_sem_t;

extern kern_err_t kern_sem_init(kern_sem_t *sem, int32_t initial, int32_t max);
extern kern_err_t kern_sem_give(kern_sem_t *sem);
extern kern_err_t kern_sem_take(kern_sem_t *sem, uint32_t timeout_ms);
extern int32_t     kern_sem_get_count(kern_sem_t *sem);

/* ═══ 优先级继承互斥锁 ═══ */

/**
 * @brief 带优先级继承的互斥锁
 *
 * 当高优先级任务等待被低优先级任务持有的锁时，
 * 临时提升低优先级任务的优先级到等待者的最高优先级。
 * 解锁时恢复原始优先级。
 */
typedef struct {
    volatile bool     locked;           /* 是否被持有 */
    xeros_spinlock_t  lock;             /* SMP 保护 */
    kern_task_t      *owner;            /* 持有者 TCB */
    uint8_t           recursive_count;  /* 递归加锁计数 */
    uint8_t           orig_priority;    /* 持有者基线优先级（PI 恢复目标，已过时，保留兼容） */
    kern_wait_node_t *wait_queue;       /* 等待队列（按优先级排序） */
} kern_pi_mutex_t;

extern kern_err_t kern_pi_mutex_init(kern_pi_mutex_t *m);
extern kern_err_t kern_pi_mutex_lock(kern_pi_mutex_t *m, uint32_t timeout_ms);
extern kern_err_t kern_pi_mutex_unlock(kern_pi_mutex_t *m);

/* ═══ 消息队列 ═══ */

/**
 * @brief 消息队列
 *
 * 固定大小的环形缓冲区，支持阻塞读写。
 */
typedef struct {
    uint8_t          *buffer;       /* 数据缓冲区 */
    size_t            msg_size;     /* 单条消息大小（字节） */
    size_t            capacity;     /* 最大消息数 */
    size_t            count;        /* 当前消息数 */
    size_t            head;         /* 读索引 */
    size_t            tail;         /* 写索引 */
    xeros_spinlock_t  lock;         /* SMP 保护 */
    kern_wait_node_t *recv_wait;    /* 等待接收的队列 */
    kern_wait_node_t *send_wait;    /* 等待发送的队列 */
} kern_queue_t;

extern kern_err_t kern_queue_init(kern_queue_t *q, void *buf,
                                   size_t msg_size, size_t capacity);
extern kern_err_t kern_queue_send(kern_queue_t *q, const void *msg,
                                   uint32_t timeout_ms);
extern kern_err_t kern_queue_recv(kern_queue_t *q, void *msg,
                                   uint32_t timeout_ms);
extern size_t     kern_queue_count(kern_queue_t *q);

/* ═══ 事件组 ═══ */

/**
 * @brief 事件组
 *
 * 32 位事件标志，支持 AND/OR 等待和自动清除。
 */
typedef struct {
    volatile uint32_t  bits;         /* 当前事件位 */
    xeros_spinlock_t   lock;         /* SMP 保护 */
    kern_wait_node_t  *wait_queue;   /* 等待队列 */
} kern_event_t;

/** 等待选项 */
#define KERN_EVENT_WAIT_ANY  0x00   /* OR 等待：任一位满足即唤醒 */
#define KERN_EVENT_WAIT_ALL  0x01   /* AND 等待：所有位满足才唤醒 */
#define KERN_EVENT_CLEAR     0x02   /* 唤醒后自动清除匹配位 */

extern kern_err_t kern_event_init(kern_event_t *ev);
extern kern_err_t kern_event_set(kern_event_t *ev, uint32_t bits);
extern kern_err_t kern_event_clear(kern_event_t *ev, uint32_t bits);
extern uint32_t   kern_event_get(kern_event_t *ev);
extern kern_err_t kern_event_wait(kern_event_t *ev, uint32_t bits,
                                   uint32_t flags, uint32_t timeout_ms);

/* ═══ 内部辅助 ═══ */

/**
 * @brief 将任务加入等待队列（按优先级降序插入）
 * @param queue  等待队列头指针
 * @param node   等待节点（调用者分配）
 */
extern void ipc_wait_enqueue(kern_wait_node_t **queue, kern_wait_node_t *node);

/**
 * @brief 从等待队列唤醒第一个任务
 * @param queue  等待队列头指针
 * @return 被唤醒的任务，队列为空返回 NULL
 */
extern kern_task_t *ipc_wait_dequeue(kern_wait_node_t **queue);

/**
 * @brief 检查并处理等待队列中超时的任务
 * @param queue  等待队列头指针
 */
extern void ipc_wait_check_timeouts(kern_wait_node_t **queue);

/**
 * @brief 阻塞当前任务并将其加入等待队列
 * @param queue      等待队列头指针
 * @param lock       保护等待队列的锁
 * @param timeout_ms 超时时间（毫秒），0 表示永不超时
 */
extern void ipc_block_task(kern_wait_node_t **queue, xeros_spinlock_t *lock,
                           uint32_t timeout_ms);

/**
 * @brief 从等待队列唤醒第一个任务
 * @param queue  等待队列头指针
 */
extern void ipc_wake_one(kern_wait_node_t **queue);

#ifdef __cplusplus
}
#endif

#endif /* KERN_IPC_H */
