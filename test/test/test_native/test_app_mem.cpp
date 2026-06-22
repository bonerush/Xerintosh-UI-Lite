/**
 * @file   test_app_mem.cpp
 * @brief  App 层统一内存视图单元测试
 * @details 测试 xeros_mem_get_stats / xeros_mem_available_bytes / xeros_mem_can_alloc。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "app/app_mem.h"
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_kmalloc.h"
#include "kernel/kern_init.h"
}

TEST(AppMemTest, MemGetStatsWrapsKernel)
{
    kern_init();
    kern_sched_init();

    kern_kmem_stat_t st;
    EXPECT_TRUE(xeros_mem_get_stats(&st));

    kern_kmem_stat_t kernel_st;
    EXPECT_TRUE(kern_kmem_get_stats(&kernel_st));

    EXPECT_EQ(st.allocated_bytes, kernel_st.allocated_bytes);
}

TEST(AppMemTest, MemAvailableZeroWhenReservedExceedsFree)
{
    kern_init();
    kern_sched_init();

    kern_kmem_stat_t st;
    ASSERT_TRUE(xeros_mem_get_stats(&st));

    /* 设置一个远超当前 allocated 的保留水位，使可用内存为 0 */
    kern_kmem_set_reserved_bytes(st.allocated_bytes + 1024 * 1024);
    EXPECT_EQ(xeros_mem_available_bytes(), (uint32_t)0);

    kern_kmem_set_reserved_bytes(0);
}

TEST(AppMemTest, MemCanAllocNativeBudget)
{
    kern_init();
    kern_sched_init();

    kern_kmem_stat_t st;
    ASSERT_TRUE(xeros_mem_get_stats(&st));

    /* native 默认预算 256KB，小分配应通过 */
    kern_kmem_set_reserved_bytes(st.allocated_bytes + 1024);
    EXPECT_TRUE(xeros_mem_can_alloc(512, 512));

    kern_kmem_set_reserved_bytes(0);
}

TEST(AppMemTest, MemCanAllocFailsOverBudget)
{
    kern_init();
    kern_sched_init();

    /* 设置很低的保留水位，使大分配失败 */
    kern_kmem_set_reserved_bytes(1024);
    EXPECT_FALSE(xeros_mem_can_alloc(256 * 1024, 256 * 1024));

    kern_kmem_set_reserved_bytes(0);
}
