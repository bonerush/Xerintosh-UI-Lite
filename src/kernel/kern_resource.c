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

/* 资源链表自旋锁（避免循环包含 kern_sync.h，直接使用编译器内置原子操作） */
#ifdef CONFIG_SMP_ENABLED
#define _resource_lock(task) do { \
    while (__sync_lock_test_and_set(&(task)->resource_lock, true)) { \
        /* spin */ \
    } \
} while (0)
#define _resource_unlock(task) do { \
    __sync_lock_release(&(task)->resource_lock); \
} while (0)
#else
#define _resource_lock(task)   ((void)0)
#define _resource_unlock(task) ((void)0)
#endif

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
    _resource_lock(task);
    res->next = task->resource_head;
    task->resource_head = res;
    _resource_unlock(task);

    return KERN_OK;
}

kern_err_t kern_resource_untrack(kern_task_t *task, void *ptr)
{
    if (task == NULL || ptr == NULL) {
        return KERN_EINVAL;
    }

    _resource_lock(task);

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
            _resource_unlock(task);
            kern_kfree_untracked(cur);
            return KERN_OK;
        }
        prev = cur;
        cur = cur->next;
    }

    _resource_unlock(task);
    return KERN_ENOENT;  /* 资源未找到 */
}

void kern_resource_release_all(kern_task_t *task)
{
    if (task == NULL) return;

    _resource_lock(task);

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

    _resource_unlock(task);
}
