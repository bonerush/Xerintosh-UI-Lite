/**
 * @file   kern_kmalloc.c
 * @brief  Xeros 内核统一内存分配器实现
 * @details 包装标准 malloc/free，在每个分配块前附加元数据头
 *          （大小 + 所有者任务指针）。分配时自动注册到当前任务的
 *          资源追踪链表，释放时自动取消注册。任务退出时，所有未释放
 *          的分配块通过 kern_resource_release_all 自动回收。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_kmalloc.h"
#include "kern_resource.h"
#include "kern_task.h"

#include <stdlib.h>
#include <string.h>

/* ═══ 分配头结构 ═══ */

/**
 * @brief 内核内存分配元数据头
 * @note  存储在用户可见指针之前，用于追踪所有权和大小
 */
typedef struct {
    size_t size;         /* 用户请求的分配大小（不含头） */
    kern_task_t *owner;  /* 拥有此分配的任务 */
} kmalloc_header_t;

/**
 * @brief 获取用户指针对应的分配头
 */
static inline kmalloc_header_t *get_header(void *ptr)
{
    if (ptr == NULL) return NULL;
    return (kmalloc_header_t *)((uint8_t *)ptr - sizeof(kmalloc_header_t));
}

/* ═══ 内存释放回调（供资源追踪使用） ═══ */

static void kmem_release(void *ptr)
{
    if (ptr == NULL) return;
    kmalloc_header_t *hdr = get_header(ptr);
    free(hdr);
}

/* ═══ 分配 API ═══ */

void *kern_kmalloc(size_t size)
{
    if (size == 0) return NULL;

    /* 防止溢出 */
    size_t total = sizeof(kmalloc_header_t) + size;
    if (total < size) return NULL;  /* 溢出检查 */

    kmalloc_header_t *hdr = (kmalloc_header_t *)malloc(total);
    if (hdr == NULL) return NULL;

    hdr->size = size;
    hdr->owner = kern_task_current();

    void *user_ptr = (void *)((uint8_t *)hdr + sizeof(kmalloc_header_t));

    /* 追踪到当前任务 */
    int ret = kern_resource_track(hdr->owner, user_ptr,
                                  KERN_RES_MEMORY, kmem_release);
    if (ret != KERN_OK) {
        /* 追踪失败，释放分配（兼容性保护） */
        free(hdr);
        return NULL;
    }

    return user_ptr;
}

void *kern_kcalloc(size_t nmemb, size_t size)
{
    /* 防止溢出 */
    size_t total;
    if (nmemb > 0 && size > (size_t)(-1) / nmemb) return NULL;
    total = nmemb * size;
    if (total == 0) return NULL;

    void *ptr = kern_kmalloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void kern_kfree(void *ptr)
{
    if (ptr == NULL) return;

    kmalloc_header_t *hdr = get_header(ptr);
    if (hdr == NULL) return;

    /* 从资源追踪中移除 */
    if (hdr->owner != NULL) {
        kern_resource_untrack(hdr->owner, ptr);
    }

    free(hdr);
}

void *kern_krealloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        /* 行为同 malloc */
        return kern_kmalloc(new_size);
    }

    if (new_size == 0) {
        /* 行为同 free */
        kern_kfree(ptr);
        return NULL;
    }

    kmalloc_header_t *old_hdr = get_header(ptr);

    /* 从旧任务的追踪中移除 */
    if (old_hdr->owner != NULL) {
        kern_resource_untrack(old_hdr->owner, ptr);
    }

    /* 重新分配 */
    kmalloc_header_t *new_hdr = (kmalloc_header_t *)realloc(old_hdr,
                                            sizeof(kmalloc_header_t) + new_size);
    if (new_hdr == NULL) {
        /* realloc 失败：旧内存仍有效，重新追踪 */
        if (old_hdr->owner != NULL) {
            kern_resource_track(old_hdr->owner, ptr,
                                KERN_RES_MEMORY, kmem_release);
        }
        return NULL;
    }

    new_hdr->size = new_size;
    new_hdr->owner = kern_task_current();

    void *user_ptr = (void *)((uint8_t *)new_hdr + sizeof(kmalloc_header_t));

    /* 追踪到新任务（可能与旧任务不同） */
    int ret = kern_resource_track(new_hdr->owner, user_ptr,
                                  KERN_RES_MEMORY, kmem_release);
    if (ret != KERN_OK) {
        free(new_hdr);
        return NULL;
    }

    return user_ptr;
}
