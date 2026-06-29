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
#include "kern_init.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef NATIVE_TEST
#include <esp_heap_caps.h>
#include <esp_system.h>
#endif

/* ═══ 分配头结构 ═══ */

/**
 * @brief 内核内存分配元数据头
 * @note  存储在用户可见指针之前，用于追踪所有权和大小
 */
typedef struct {
    size_t size;         /* 用户请求的分配大小（不含头） */
    kern_task_t *owner;  /* 拥有此分配的任务 */
    uint32_t canary;     /* 金丝雀值 0xDEADBEEF，检测内存越界写 */
} kmalloc_header_t;

/**
 * @brief 获取用户指针对应的分配头
 */
static inline kmalloc_header_t *get_header(void *ptr)
{
    if (ptr == NULL) return NULL;
    return (kmalloc_header_t *)((uint8_t *)ptr - sizeof(kmalloc_header_t));
}

/* ═══ 全局内存统计（原子操作） ═══ */

static size_t g_kmem_allocated_bytes = 0;

/* 用 canary 包围保留水位，检测是否有越界写或非法修改 */
static struct {
    uint32_t canary_before;
    volatile size_t reserved;
    uint32_t canary_after;
} g_kmem_reserved_guard = { 0xDEADBEEF, 0, 0xBEEFDEAD };

/* ═══ 内存释放回调（供资源追踪使用） ═══ */

static void kmem_release(void *ptr)
{
    if (ptr == NULL) return;
    kmalloc_header_t *hdr = get_header(ptr);
    free(hdr);
}

/* ═══ 内部分配实现 ═══ */

static void *kern_kmalloc_impl(size_t size, kern_task_t *owner, bool track)
{
    if (size == 0) return NULL;

    /* 防止溢出 */
    size_t total = sizeof(kmalloc_header_t) + size;
    if (total < size) return NULL;  /* 溢出检查 */

    kmalloc_header_t *hdr = (kmalloc_header_t *)malloc(total);
    if (hdr == NULL) return NULL;

    hdr->size = size;
    hdr->owner = owner;
    hdr->canary = 0xDEADBEEF;

    void *user_ptr = (void *)((uint8_t *)hdr + sizeof(kmalloc_header_t));

    if (track) {
        /* 追踪到当前任务 */
        int ret = kern_resource_track(owner, user_ptr,
                                      KERN_RES_MEMORY, kmem_release);
        if (ret != KERN_OK) {
            /* 追踪失败，释放分配（兼容性保护） */
            free(hdr);
            return NULL;
        }
    }

    __sync_fetch_and_add(&g_kmem_allocated_bytes, size);

    return user_ptr;
}

/* ═══ 分配 API ═══ */

void *kern_kmalloc(size_t size)
{
    return kern_kmalloc_impl(size, kern_task_current(), true);
}

void *kern_kmalloc_for_task(kern_task_t *task, size_t size)
{
    if (task == NULL) return NULL;
    return kern_kmalloc_impl(size, task, true);
}

void *kern_kmalloc_untracked(size_t size)
{
    return kern_kmalloc_impl(size, NULL, false);
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

    /* 验证金丝雀值，检测内存越界写 */
    if (hdr->canary != 0xDEADBEEF) {
        kern_log(KERN_LOG_ERROR,
                 "kmalloc canary corrupted (ptr=%p, size=%zu, canary=0x%08lx)",
                 ptr, hdr->size, (unsigned long)hdr->canary);
    }

    /* 从资源追踪中移除 */
    if (hdr->owner != NULL) {
        kern_resource_untrack(hdr->owner, ptr);
    }

    __sync_fetch_and_sub(&g_kmem_allocated_bytes, hdr->size);

    free(hdr);
}

void kern_kfree_untracked(void *ptr)
{
    if (ptr == NULL) return;

    kmalloc_header_t *hdr = get_header(ptr);
    if (hdr == NULL) return;

    /* 验证金丝雀值，检测内存越界写 */
    if (hdr->canary != 0xDEADBEEF) {
        kern_log(KERN_LOG_ERROR,
                 "kmalloc untracked canary corrupted (ptr=%p, size=%zu, canary=0x%08lx)",
                 ptr, hdr->size, (unsigned long)hdr->canary);
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

    /* 先扣除旧大小；成功后再加回新大小（原子操作） */
    size_t old_size = old_hdr->size;
    __sync_fetch_and_sub(&g_kmem_allocated_bytes, old_size);

    /* 重新分配 */
    kmalloc_header_t *new_hdr = (kmalloc_header_t *)realloc(old_hdr,
                                            sizeof(kmalloc_header_t) + new_size);
    if (new_hdr == NULL) {
        /* realloc 失败：旧内存仍有效，恢复统计并重新追踪 */
        __sync_fetch_and_add(&g_kmem_allocated_bytes, old_size);
        if (old_hdr->owner != NULL) {
            kern_resource_track(old_hdr->owner, ptr,
                                KERN_RES_MEMORY, kmem_release);
        }
        return NULL;
    }

    new_hdr->size = new_size;
    new_hdr->owner = kern_task_current();
    new_hdr->canary = 0xDEADBEEF;
    __sync_fetch_and_add(&g_kmem_allocated_bytes, new_size);

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

/* ═══ 内存统计 API ═══ */

bool kern_kmem_get_stats(kern_kmem_stat_t *out)
{
    if (out == NULL) return false;

    memset(out, 0, sizeof(*out));
    out->allocated_bytes = __sync_fetch_and_add(&g_kmem_allocated_bytes, 0);

#ifndef NATIVE_TEST
    out->total_bytes = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    out->free_bytes  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    out->largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    out->min_free_bytes = esp_get_minimum_free_heap_size();

    if (out->free_bytes > 0 && out->largest_free_block <= out->free_bytes) {
        out->fragmentation_percent =
            100 - (out->largest_free_block * 100 / out->free_bytes);
    } else {
        out->fragmentation_percent = 0;
    }
#else
    /* native 环境无统一堆接口，只返回 kmalloc 自身统计 */
    out->free_bytes = 0;
    out->largest_free_block = 0;
    out->min_free_bytes = 0;
    out->fragmentation_percent = 0;
#endif

    return true;
}

/* ═══ 内存压力等级与保留内存 ═══ */

#define KERN_KMEM_PRESSURE_HIGH_PCT  10
#define KERN_KMEM_PRESSURE_LOW_PCT   25
#define KERN_KMEM_PRESSURE_HIGH_FRAG 50

kern_kmem_pressure_level_t kern_kmem_pressure_level(void)
{
    kern_kmem_stat_t st;
    if (!kern_kmem_get_stats(&st)) return KERN_KMEM_PRESSURE_LOW;

#ifndef NATIVE_TEST
    if (st.total_bytes == 0) return KERN_KMEM_PRESSURE_LOW;
    size_t free_pct = st.free_bytes * 100 / st.total_bytes;
    if (free_pct < KERN_KMEM_PRESSURE_HIGH_PCT) return KERN_KMEM_PRESSURE_HIGH;
    if (free_pct < KERN_KMEM_PRESSURE_LOW_PCT ||
        st.fragmentation_percent > KERN_KMEM_PRESSURE_HIGH_FRAG) {
        return KERN_KMEM_PRESSURE_MEDIUM;
    }
    return KERN_KMEM_PRESSURE_LOW;
#else
    /* Native 无真实堆总大小，基于保留水位判断 */
    if (g_kmem_reserved_guard.reserved == 0) return KERN_KMEM_PRESSURE_LOW;
    if (st.allocated_bytes > g_kmem_reserved_guard.reserved) return KERN_KMEM_PRESSURE_HIGH;
    if (st.allocated_bytes > g_kmem_reserved_guard.reserved * 3 / 4) return KERN_KMEM_PRESSURE_MEDIUM;
    return KERN_KMEM_PRESSURE_LOW;
#endif
}

void kern_kmem_set_reserved_bytes(size_t bytes)
{
    g_kmem_reserved_guard.reserved = bytes;
}

size_t kern_kmem_reserved_bytes(void)
{
    /* 保留水位：仅由 kern_kmem_set_reserved_bytes() 修改，默认 0 */
    if (g_kmem_reserved_guard.canary_before != 0xDEADBEEF ||
        g_kmem_reserved_guard.canary_after != 0xBEEFDEAD) {
        kern_log(KERN_LOG_ERROR,
                 "reserved guard corrupted: before=0x%08lx after=0x%08lx",
                 (unsigned long)g_kmem_reserved_guard.canary_before,
                 (unsigned long)g_kmem_reserved_guard.canary_after);
        g_kmem_reserved_guard.canary_before = 0xDEADBEEF;
        g_kmem_reserved_guard.canary_after = 0xBEEFDEAD;
    }
    return g_kmem_reserved_guard.reserved;
}
