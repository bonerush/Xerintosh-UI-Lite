/**
 * @file   kern_ipc.c
 * @brief  Xeros IPC 原语实现
 * @details 二值信号量、计数信号量、优先级继承互斥锁、消息队列、事件组。
 *
 * 所有阻塞操作通过将当前任务加入等待队列并调用 kern_yield() 实现。
 * 超时通过 kern_sleep_ms 的 wake_time 机制实现。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_ipc.h"
#include "kern_sched.h"
#include "kern_smp.h"
#include "kern_kmalloc.h"

#include <string.h>

/* ═══ 等待队列辅助 ═══ */

/**
 * @brief 按优先级降序插入等待队列
 *
 * 高优先级任务排在前面，同优先级 FIFO。
 */
void ipc_wait_enqueue(kern_wait_node_t **queue, kern_wait_node_t *node)
{
    node->next = NULL;
    kern_task_t *task = node->task;

    if (*queue == NULL) {
        *queue = node;
        return;
    }

    /* 按优先级降序插入 */
    if (task->priority > (*queue)->task->priority) {
        node->next = *queue;
        *queue = node;
        return;
    }

    kern_wait_node_t *prev = *queue;
    while (prev->next != NULL && prev->next->task->priority >= task->priority) {
        prev = prev->next;
    }
    node->next = prev->next;
    prev->next = node;
}

kern_task_t *ipc_wait_dequeue(kern_wait_node_t **queue)
{
    if (*queue == NULL) return NULL;

    kern_wait_node_t *node = *queue;
    *queue = node->next;
    node->next = NULL;

    return node->task;
}

void ipc_wait_check_timeouts(kern_wait_node_t **queue)
{
    if (*queue == NULL) return;

    uint32_t now = g_sched_ticks;

    kern_wait_node_t **pp = queue;
    while (*pp != NULL) {
        kern_wait_node_t *node = *pp;
        if (node->timeout != 0 && node->timeout <= now) {
            /* 超时：从队列移除，唤醒任务 */
            *pp = node->next;
            node->next = NULL;
            if (node->task != NULL) {
                node->task->state = KERN_TASK_READY;
            }
        } else {
            pp = &node->next;
        }
    }
}

/* ═══ 内部：阻塞当前任务 ═══ */

static void ipc_block_task(kern_wait_node_t **queue, uint32_t timeout_ms)
{
    kern_task_t *current = kern_task_current();
    if (current == NULL) return;

    kern_wait_node_t node = {
        .task = current,
        .next = NULL,
        .timeout = (timeout_ms > 0) ?
            (g_sched_ticks + timeout_ms) : 0,
    };

    current->state = KERN_TASK_SLEEPING;
    ipc_wait_enqueue(queue, &node);
    kern_yield();

    /* 被唤醒后返回 */
}

static void ipc_wake_one(kern_wait_node_t **queue)
{
    kern_task_t *task = ipc_wait_dequeue(queue);
    if (task != NULL) {
        task->state = KERN_TASK_READY;
    }
}

/* ═══ 二值信号量 ═══ */

kern_err_t kern_bin_sem_give(kern_bin_sem_t *sem)
{
    if (sem == NULL) return KERN_EINVAL;

    if (sem->count < 1) {
        sem->count = 1;
        ipc_wake_one(&sem->wait_queue);
    }
    return KERN_OK;
}

kern_err_t kern_bin_sem_take(kern_bin_sem_t *sem, uint32_t timeout_ms)
{
    if (sem == NULL) return KERN_EINVAL;

    if (sem->count > 0) {
        sem->count = 0;
        return KERN_OK;
    }

    if (timeout_ms == 0) return KERN_ETIMEOUT;

    ipc_block_task(&sem->wait_queue, timeout_ms);

    /* 被唤醒后检查是否真的拿到了 */
    if (sem->count > 0) {
        sem->count = 0;
        return KERN_OK;
    }
    return KERN_ETIMEOUT;
}

/* ═══ 计数信号量 ═══ */

kern_err_t kern_sem_init(kern_sem_t *sem, int32_t initial, int32_t max)
{
    if (sem == NULL || max <= 0 || initial < 0 || initial > max)
        return KERN_EINVAL;

    sem->count = initial;
    sem->max_count = max;
    sem->wait_queue = NULL;
    return KERN_OK;
}

kern_err_t kern_sem_give(kern_sem_t *sem)
{
    if (sem == NULL) return KERN_EINVAL;

    if (sem->count < sem->max_count) {
        sem->count++;
        ipc_wake_one(&sem->wait_queue);
    }
    return KERN_OK;
}

kern_err_t kern_sem_take(kern_sem_t *sem, uint32_t timeout_ms)
{
    if (sem == NULL) return KERN_EINVAL;

    if (sem->count > 0) {
        sem->count--;
        return KERN_OK;
    }

    if (timeout_ms == 0) return KERN_ETIMEOUT;

    ipc_block_task(&sem->wait_queue, timeout_ms);

    if (sem->count > 0) {
        sem->count--;
        return KERN_OK;
    }
    return KERN_ETIMEOUT;
}

int32_t kern_sem_get_count(kern_sem_t *sem)
{
    if (sem == NULL) return 0;
    return sem->count;
}

/* ═══ 优先级继承互斥锁 ═══ */

kern_err_t kern_pi_mutex_init(kern_pi_mutex_t *m)
{
    if (m == NULL) return KERN_EINVAL;

    m->locked = false;
    m->owner = NULL;
    m->recursive_count = 0;
    m->orig_priority = 0;
    m->wait_queue = NULL;
    return KERN_OK;
}

/**
 * @brief 优先级继承：提升持有者优先级
 */
static void pi_boost_owner(kern_pi_mutex_t *m)
{
    kern_task_t *current = kern_task_current();
    if (current == NULL || m->owner == NULL) return;

    if (current->priority > m->owner->priority) {
        if (m->owner->priority != m->orig_priority) {
            /* 已经被提升过，只取最高值 */
            if (current->priority > m->owner->priority) {
                m->owner->priority = current->priority;
            }
        } else {
            /* 首次提升：记录原始优先级 */
            m->orig_priority = m->owner->priority;
            m->owner->priority = current->priority;
        }
    }
}

/**
 * @brief 优先级继承：恢复持有者原始优先级
 */
static void pi_restore_owner(kern_pi_mutex_t *m)
{
    if (m->owner != NULL && m->orig_priority != 0) {
        m->owner->priority = m->orig_priority;
        m->orig_priority = 0;
    }
}

kern_err_t kern_pi_mutex_lock(kern_pi_mutex_t *m, uint32_t timeout_ms)
{
    if (m == NULL) return KERN_EINVAL;

    kern_task_t *current = kern_task_current();
    if (current == NULL) return KERN_EPERM;

    /* 递归加锁 */
    if (m->owner == current) {
        m->recursive_count++;
        return KERN_OK;
    }

    /* 尝试获取锁 */
    if (!m->locked) {
        m->locked = true;
        m->owner = current;
        m->recursive_count = 1;
        m->orig_priority = current->priority;
        return KERN_OK;
    }

    /* 锁被持有：执行优先级继承 */
    pi_boost_owner(m);

    if (timeout_ms == 0) return KERN_ETIMEOUT;

    /* 阻塞等待 */
    ipc_block_task(&m->wait_queue, timeout_ms);

    /* 被唤醒后检查是否拿到锁 */
    if (m->owner == current) {
        return KERN_OK;
    }
    return KERN_ETIMEOUT;
}

kern_err_t kern_pi_mutex_unlock(kern_pi_mutex_t *m)
{
    if (m == NULL) return KERN_EINVAL;

    kern_task_t *current = kern_task_current();
    if (m->owner != current) return KERN_EPERM;

    if (m->recursive_count > 0) {
        m->recursive_count--;
    }

    if (m->recursive_count == 0) {
        /* 恢复原始优先级 */
        pi_restore_owner(m);

        /* 释放锁 */
        m->locked = false;
        m->owner = NULL;

        /* 唤醒等待者 */
        kern_task_t *next = ipc_wait_dequeue(&m->wait_queue);
        if (next != NULL) {
            m->locked = true;
            m->owner = next;
            m->recursive_count = 1;
            m->orig_priority = next->priority;
            next->state = KERN_TASK_READY;
        }
    }

    return KERN_OK;
}

/* ═══ 消息队列 ═══ */

kern_err_t kern_queue_init(kern_queue_t *q, void *buf,
                            size_t msg_size, size_t capacity)
{
    if (q == NULL || buf == NULL || msg_size == 0 || capacity == 0)
        return KERN_EINVAL;

    q->buffer = (uint8_t *)buf;
    q->msg_size = msg_size;
    q->capacity = capacity;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
    q->recv_wait = NULL;
    q->send_wait = NULL;
    return KERN_OK;
}

kern_err_t kern_queue_send(kern_queue_t *q, const void *msg, uint32_t timeout_ms)
{
    if (q == NULL || msg == NULL) return KERN_EINVAL;

    /* 队列满：等待或返回 */
    while (q->count >= q->capacity) {
        if (timeout_ms == 0) return KERN_ETIMEOUT;
        ipc_block_task(&q->send_wait, timeout_ms);
        /* 被唤醒后重试 */
    }

    /* 写入消息 */
    memcpy(q->buffer + (q->tail * q->msg_size), msg, q->msg_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    /* 唤醒等待接收的任务 */
    ipc_wake_one(&q->recv_wait);

    return KERN_OK;
}

kern_err_t kern_queue_recv(kern_queue_t *q, void *msg, uint32_t timeout_ms)
{
    if (q == NULL || msg == NULL) return KERN_EINVAL;

    /* 队列空：等待或返回 */
    while (q->count == 0) {
        if (timeout_ms == 0) return KERN_ETIMEOUT;
        ipc_block_task(&q->recv_wait, timeout_ms);
    }

    /* 读取消息 */
    memcpy(msg, q->buffer + (q->head * q->msg_size), q->msg_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    /* 唤醒等待发送的任务 */
    ipc_wake_one(&q->send_wait);

    return KERN_OK;
}

size_t kern_queue_count(kern_queue_t *q)
{
    if (q == NULL) return 0;
    return q->count;
}

/* ═══ 事件组 ═══ */

kern_err_t kern_event_init(kern_event_t *ev)
{
    if (ev == NULL) return KERN_EINVAL;
    ev->bits = 0;
    ev->wait_queue = NULL;
    return KERN_OK;
}

kern_err_t kern_event_set(kern_event_t *ev, uint32_t bits)
{
    if (ev == NULL) return KERN_EINVAL;

    ev->bits |= bits;

    /* 唤醒所有满足条件的等待者 */
    kern_wait_node_t **pp = &ev->wait_queue;
    while (*pp != NULL) {
        kern_wait_node_t *node = *pp;
        /* 简化：唤醒所有等待者，让它们自己检查条件 */
        kern_task_t *task = node->task;
        if (task != NULL) {
            task->state = KERN_TASK_READY;
        }
        *pp = node->next;
        node->next = NULL;
    }

    return KERN_OK;
}

kern_err_t kern_event_clear(kern_event_t *ev, uint32_t bits)
{
    if (ev == NULL) return KERN_EINVAL;
    ev->bits &= ~bits;
    return KERN_OK;
}

uint32_t kern_event_get(kern_event_t *ev)
{
    if (ev == NULL) return 0;
    return ev->bits;
}

kern_err_t kern_event_wait(kern_event_t *ev, uint32_t bits,
                            uint32_t flags, uint32_t timeout_ms)
{
    if (ev == NULL) return KERN_EINVAL;

    bool wait_all = (flags & KERN_EVENT_WAIT_ALL) != 0;
    bool auto_clear = (flags & KERN_EVENT_CLEAR) != 0;

    /* 检查条件是否已满足 */
    uint32_t current = ev->bits;
    bool satisfied = wait_all ? ((current & bits) == bits) : ((current & bits) != 0);

    if (satisfied) {
        if (auto_clear) {
            ev->bits &= ~bits;
        }
        return KERN_OK;
    }

    if (timeout_ms == 0) return KERN_ETIMEOUT;

    /* 阻塞等待 */
    ipc_block_task(&ev->wait_queue, timeout_ms);

    /* 被唤醒后重新检查 */
    current = ev->bits;
    satisfied = wait_all ? ((current & bits) == bits) : ((current & bits) != 0);

    if (satisfied) {
        if (auto_clear) {
            ev->bits &= ~bits;
        }
        return KERN_OK;
    }

    return KERN_ETIMEOUT;
}
