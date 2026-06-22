/**
 * @file   kern_task_virtual.c
 * @brief  Xeros 虚任务管理实现
 * @details 虚任务注册/注销，用于内核可观测性（/proc/tasks 可见、
 *          kill 可终止），不创建独立上下文，不参与调度。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched.h"
#include "kern_task.h"
#include "kern_smp.h"
#include "kern_init.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ═══ 虚任务注册 ═══ */

kern_pid_t kern_task_register_virtual(const char *name)
{
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_RUNNING;
    task->flags = KERN_TASK_FLAG_VIRTUAL;
    task->priority = 128;
    task->stack_base = NULL;
    task->stack_size = 0;
    task->entry = NULL;
    task->arg = NULL;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "vtask_%d", task->pid);
    }

    /* 插入任务链表头部 */
#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
#endif
    task->next = g_task_list;
    g_task_list = task;
    g_task_count++;
#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif

    kern_log(KERN_LOG_DEBUG, "virtual task %d (%s) registered", task->pid, task->name);
    return task->pid;
}

/* ═══ 虚任务注销 ═══ */

void kern_task_unregister_virtual(kern_pid_t pid)
{
    kern_task_t *task = kern_task_get(pid);
    if (task == NULL) return;
    if (!(task->flags & KERN_TASK_FLAG_VIRTUAL)) {
        kern_log(KERN_LOG_WARN, "task %d is not virtual, refusing unregister", pid);
        return;
    }

    task->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "virtual task %d (%s) unregistered", task->pid, task->name);

    /* 立即从链表中移除并回收（虚任务无需等待调度器） */
#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
#endif
    kern_task_t *prev = NULL;
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;
            } else {
                g_task_list = t->next;
            }
            if (g_last_picked == task) g_last_picked = NULL;
            g_task_count--;
#ifdef CONFIG_SMP_ENABLED
            __sync_lock_release(&g_task_list_lock);
#endif
            free(task);
            return;
        }
        prev = t;
        t = t->next;
    }
#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif
}
