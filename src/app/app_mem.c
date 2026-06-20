/**
 * @file   app_mem.c
 * @brief  App 层统一内存视图实现
 * @details 包装 kern_kmem_* 接口，提供保留水位感知的安全分配判断。
 *
 * @copyright Copyright (c) 2026
 */

#include "app/app_mem.h"
#include "kernel/kern_kmalloc.h"

bool xeros_mem_get_stats(kern_kmem_stat_t *out)
{
    return kern_kmem_get_stats(out);
}

uint32_t xeros_mem_available_bytes(void)
{
    kern_kmem_stat_t st;
    if (!kern_kmem_get_stats(&st)) return 0;
    if (st.free_bytes <= kern_kmem_reserved_bytes()) return 0;
    return (uint32_t)(st.free_bytes - kern_kmem_reserved_bytes());
}

bool xeros_mem_can_alloc(uint32_t needed_bytes, uint32_t needed_contiguous)
{
    kern_kmem_stat_t st;
    if (!kern_kmem_get_stats(&st)) return false;

    size_t reserved = kern_kmem_reserved_bytes();
#ifdef NATIVE_TEST
    /* native 无真实堆/连续块信息，用 allocated_bytes 近似 */
    (void)needed_contiguous;
    size_t budget = reserved;
    if (budget == 0) budget = 256 * 1024; /* 默认 256KB 预算 */
    if (st.allocated_bytes + needed_bytes > budget) return false;
    return true;
#else
    if (st.free_bytes < (size_t)needed_bytes + reserved) return false;
    if (st.largest_free_block < (size_t)needed_contiguous + reserved) return false;
    return true;
#endif
}
