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

/* 前向声明任务控制块，避免与 kern_task.h 循环包含 */
struct kern_task;
typedef struct kern_task kern_task_t;

/* ═══ 内核分配器 API ═══ */

/**
 * @brief  分配内存（自动追踪到当前任务）
 * @param  size 分配字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *kern_kmalloc(size_t size);

/**
 * @brief  分配内存并追踪到指定任务
 * @param  task 资源所有者任务
 * @param  size 分配字节数
 * @return 分配的内存指针，失败返回 NULL
 * @note   用于任务栈等需要在目标任务运行前就绑定到其资源链表的内存
 */
void *kern_kmalloc_for_task(kern_task_t *task, size_t size);

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

/* ═══ 内存统计结构体 ═══ */

/**
 * @brief 内核内存统计快照
 * @note  部分字段在 NATIVE_TEST 后端为近似值或 0（不可用）。
 */
typedef struct {
    size_t total_bytes;           /* 堆总字节数（FreeRTOS）/ 0（native） */
    size_t free_bytes;            /* 当前空闲字节数 */
    size_t largest_free_block;    /* 最大连续空闲块 */
    size_t min_free_bytes;        /* 历史最小空闲字节数 */
    size_t allocated_bytes;       /* kmalloc 已追踪分配字节总和 */
    size_t fragmentation_percent; /* 碎片率估算 0-100（(1 - largest/free)*100） */
} kern_kmem_stat_t;

/**
 * @brief  获取当前内存统计
 * @param  out 输出结构体指针
 * @return true 成功填充，false 参数无效
 */
bool kern_kmem_get_stats(kern_kmem_stat_t *out);

/* ═══ 内存压力等级 ═══ */

/**
 * @brief 内存压力等级
 */
typedef enum {
    KERN_KMEM_PRESSURE_LOW = 0,    /* 空闲充足、碎片率低 */
    KERN_KMEM_PRESSURE_MEDIUM,     /* 空闲中等或碎片率高 */
    KERN_KMEM_PRESSURE_HIGH,       /* 空闲严重不足 */
} kern_kmem_pressure_level_t;

/**
 * @brief  获取当前内存压力等级
 * @note  FreeRTOS: 基于 total/free/fragmentation
 *        Native: 基于 kmalloc 已分配量与保留水位
 */
kern_kmem_pressure_level_t kern_kmem_pressure_level(void);

/**
 * @brief  设置系统保留内存（字节）
 * @note   App 层应在初始化时调用，确保 xeros_mem_available 计算可用内存时扣除保留水位
 */
void kern_kmem_set_reserved_bytes(size_t bytes);

/**
 * @brief  获取当前系统保留内存（字节）
 */
size_t kern_kmem_reserved_bytes(void);

/* ═══ 便捷宏 ═══ */

#define kmalloc(sz)  kern_kmalloc(sz)
#define kfree(ptr)   kern_kfree(ptr)

#ifdef __cplusplus
}
#endif

#endif /* KERN_KMALLOC_H */
