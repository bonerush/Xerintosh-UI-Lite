/**
 * @file   kern_resource.c
 * @brief  Xeros 内核资源追踪实现
 * @details 实现资源追踪链表管理，支持在任务退出/终止时
 *          自动回收所有持有的资源（内存、互斥锁、信号量、FD 等）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_resource.h"
#include "kern_kmalloc.h"
#include "kern_sched.h"

kern_err_t kern_resource_track(kern_task_t *task, void *ptr,
                        kern_resource_type_t type, void (*release)(void *))
{
    if (task == NULL || ptr == NULL || release == NULL) {
        return KERN_EINVAL;
    }

    kern_resource_t *res = (kern_resource_t *)kern_kmalloc_untracked(sizeof(kern_resource_t));
    if (res == NULL) {
        return KERN_ENOMEM;
    }

    res->ptr = ptr;
    res->type = type;
    res->release = release;

    /* 插入到资源链表头部 */
    res->next = task->resource_head;
    task->resource_head = res;

    return KERN_OK;
}

kern_err_t kern_resource_untrack(kern_task_t *task, void *ptr)
{
    if (task == NULL || ptr == NULL) {
        return KERN_EINVAL;
    }

    kern_resource_t *prev = NULL;
    kern_resource_t *cur = task->resource_head;

    while (cur != NULL) {
        if (cur->ptr == ptr) {
            /* 从链表中移除 */
            if (prev != NULL) {
                prev->next = cur->next;
            } else {
                task->resource_head = cur->next;
            }
            kern_kfree_untracked(cur);
            return KERN_OK;
        }
        prev = cur;
        cur = cur->next;
    }

    return KERN_ENOENT;  /* 资源未找到 */
}

void kern_resource_release_all(kern_task_t *task)
{
    if (task == NULL) return;

    kern_resource_t *cur = task->resource_head;

    while (cur != NULL) {
        kern_resource_t *next = cur->next;

        /* 调用资源的释放回调 */
        if (cur->release != NULL && cur->ptr != NULL) {
            cur->release(cur->ptr);
        }

        kern_kfree_untracked(cur);
        cur = next;
    }

    task->resource_head = NULL;
}
