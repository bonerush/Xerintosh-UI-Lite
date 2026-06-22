/**
 * @file   kern_resource.h
 * @brief  Xeros 内核资源追踪头文件
 * @details 定义资源类型枚举、资源追踪链表节点结构，
 *          以及资源追踪/释放 API。用于任务退出时自动回收
 *          持有的互斥锁、分配的内存、打开的文件描述符等资源。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_RESOURCE_H
#define KERN_RESOURCE_H

#include "kern_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 资源类型 ═══ */

typedef enum {
    KERN_RES_MEMORY,      /* 堆分配 */
    KERN_RES_MUTEX,       /* 持有的互斥锁 */
    KERN_RES_SEMAPHORE,   /* 持有的信号量 */
    KERN_RES_FD,          /* 打开的文件描述符 */
    KERN_RES_FILE,        /* 打开的文件句柄 */
} kern_resource_type_t;

/* ═══ 资源追踪节点 ═══ */

typedef struct kern_resource {
    void                  *ptr;      /* 资源标识指针 */
    kern_resource_type_t   type;     /* 资源类型 */
    void                 (*release)(void *ptr);  /* 释放回调 */
    struct kern_resource  *next;     /* 链表下一节点 */
} kern_resource_t;

/* ═══ 资源追踪 API ═══ */

/**
 * @brief  追踪任务持有的资源（用于自动回收）
 * @param  task    持有资源的任务
 * @param  ptr     资源标识指针
 * @param  type    资源类型
 * @param  release 释放回调函数
 * @return KERN_OK 成功，< 0 为错误码
 */
kern_err_t kern_resource_track(kern_task_t *task, void *ptr,
                        kern_resource_type_t type, void (*release)(void *));

/**
 * @brief  移除已追踪的资源（显式释放时调用）
 * @param  task 持有资源的任务
 * @param  ptr  资源标识指针
 * @return KERN_OK 成功，< 0 为错误码
 */
kern_err_t kern_resource_untrack(kern_task_t *task, void *ptr);

/**
 * @brief  释放任务持有的所有追踪资源
 * @param  task 要清理的任务
 * @note   遍历 resource_head 链表，依次调用 release 回调
 */
void kern_resource_release_all(kern_task_t *task);

#ifdef __cplusplus
}
#endif

#endif /* KERN_RESOURCE_H */
