/**
 * @file   kern_kmalloc.h
 * @brief  Xeros 内核统一内存分配器头文件
 * @details 提供 kern_kmalloc/kern_kfree 等内核内存分配函数。
 *          分配的内存自动关联到当前任务，任务退出时自动释放。
 *          底层包装标准 malloc/free，并在分配头中存储元数据。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_KMALLOC_H
#define KERN_KMALLOC_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 内核分配器 API ═══ */

/**
 * @brief  分配内存（自动追踪到当前任务）
 * @param  size 分配字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *kern_kmalloc(size_t size);

/**
 * @brief  分配并清零内存（自动追踪到当前任务）
 * @param  nmemb 元素数量
 * @param  size  每个元素字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *kern_kcalloc(size_t nmemb, size_t size);

/**
 * @brief  释放内存（自动从当前任务取消追踪）
 * @param  ptr 要释放的内存指针（可为 NULL）
 */
void kern_kfree(void *ptr);

/**
 * @brief  分配内存（不追踪到任务，用于资源节点自身分配）
 * @param  size 分配字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *kern_kmalloc_untracked(size_t size);

/**
 * @brief  释放 untracked 内存（不操作资源追踪链表）
 * @param  ptr 要释放的内存指针（可为 NULL）
 */
void kern_kfree_untracked(void *ptr);

/**
 * @brief  重新分配内存（自动更新追踪）
 * @param  ptr      旧内存指针（可为 NULL，行为同 malloc）
 * @param  new_size 新分配字节数
 * @return 新内存指针，失败返回 NULL（旧内存不释放）
 */
void *kern_krealloc(void *ptr, size_t new_size);

/* ═══ 便捷宏 ═══ */

#define kmalloc(sz)  kern_kmalloc(sz)
#define kfree(ptr)   kern_kfree(ptr)

#ifdef __cplusplus
}
#endif

#endif /* KERN_KMALLOC_H */
