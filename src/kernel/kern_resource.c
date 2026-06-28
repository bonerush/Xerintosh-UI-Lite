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

#include <string.h>

/* ═══ 资源节点对象池 ═══
 *
 * 预分配 kern_resource_t 数组，避免 kern_resource_track() 中
 * 频繁的 kern_kmalloc_untracked / kern_kfree_untracked 开销和堆碎片。
 * 池耗尽时回退 kern_kmalloc_untracked。
 */

#define RES_POOL_SIZE (KERN_MAX_TASKS * 4)  /* 16 * 4 = 64 */

/* 编译期保证：位图 (uint64_t) 必须能容纳所有槽位 */
_Static_assert(RES_POOL_SIZE <= 64,
               "RES_POOL_SIZE exceeds 64-bit bitmap; increase bitmap type");

static kern_resource_t g_res_pool[RES_POOL_SIZE];
static uint64_t g_res_pool_bitmap;  /* 位图：bit i = 1 表示已分配 */

static kern_resource_t *res_pool_alloc(void) {
    for (int i = 0; i < RES_POOL_SIZE; i++) {
        if (!(g_res_pool_bitmap & (1ULL << i))) {
            g_res_pool_bitmap |= (1ULL << i);
            memset(&g_res_pool[i], 0, sizeof(kern_resource_t));
            return &g_res_pool[i];
        }
    }
    return NULL;  /* 池耗尽 */
}

static void res_pool_free(kern_resource_t *r) {
    if (r == NULL) return;
    int idx = (int)(r - g_res_pool);
    if (idx >= 0 && idx < RES_POOL_SIZE) {
        g_res_pool_bitmap &= ~(1ULL << idx);
    }
}

static bool res_is_pooled(kern_resource_t *r) {
    if (r == NULL) return false;
    int idx = (int)(r - g_res_pool);
    return (idx >= 0 && idx < RES_POOL_SIZE);
}

/* ═══ 内部释放辅助 ═══ */

static void res_node_free(kern_resource_t *r)
{
    if (r == NULL) return;
    if (res_is_pooled(r)) {
        res_pool_free(r);
    } else {
        kern_kfree_untracked(r);
    }
}

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

    kern_resource_t *res = res_pool_alloc();
    if (res == NULL) {
        /* 池耗尽：回退 kern_kmalloc_untracked */
        res = (kern_resource_t *)kern_kmalloc_untracked(sizeof(kern_resource_t));
    }
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
            res_node_free(cur);
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

        res_node_free(cur);
        cur = next;
    }

    task->resource_head = NULL;

    _resource_unlock(task);
}
