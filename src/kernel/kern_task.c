/**
 * @file   kern_task.c
 * @brief  Xeros 任务管理统一入口
 * @details 聚合调度器、任务生命周期、栈管理、虚任务等子模块。
 *          提供查询接口（current/get/list_head）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_task.h"
#include "kern_sched.h"
#include "kern_init.h"

#include <string.h>

/* ═══ 查询接口 ═══ */

kern_task_t *kern_task_current(void)
{
    return g_current_task;
}

kern_task_t *kern_task_get(kern_pid_t pid)
{
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->pid == pid) return t;
        t = t->next;
    }
    return NULL;
}
