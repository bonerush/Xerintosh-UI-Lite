/**
 * @file   test_kernel_kmalloc.cpp
 * @brief  Xeros 内核分配器单元测试
 * @details 测试 kern_kmalloc / kern_kfree / kern_kcalloc / kern_krealloc。
 *          验证分配头正确性、资源追踪自动关联、释放后清理。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_resource.h"
#include "kernel/kern_kmalloc.h"
#include "kernel/kern_init.h"
}

/* ═══ 辅助任务入口 ═══ */

static void noop_task(void *arg)
{
    (void)arg;
    kern_exit();
}

/* ═══ 基本分配测试 ═══ */

TEST(KernelKmallocTest, KmallocReturnsNonNull)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kmalloc(64);
    ASSERT_NE(ptr, nullptr);
    kern_kfree(ptr);
}

TEST(KernelKmallocTest, KmallocZeroSizeReturnsNull)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kmalloc(0);
    EXPECT_EQ(ptr, nullptr);
}

TEST(KernelKmallocTest, KfreeNullDoesNotCrash)
{
    kern_kfree(NULL);
    /* 不应崩溃 */
}

TEST(KernelKmallocTest, KcallocReturnsZeroedMemory)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kcalloc(16, 4);
    ASSERT_NE(ptr, nullptr);

    uint8_t *bytes = (uint8_t *)ptr;
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(bytes[i], 0) << "Byte " << i << " is not zero";
    }

    kern_kfree(ptr);
}

TEST(KernelKmallocTest, KcallocZeroNmembReturnsNull)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kcalloc(0, 100);
    EXPECT_EQ(ptr, nullptr);
}

TEST(KernelKmallocTest, KcallocOverflowReturnsNull)
{
    kern_init();
    kern_sched_init();

    /* 2 * SIZE_MAX/2+1 会产生溢出 */
    void *ptr = kern_kcalloc(2, (size_t)(-1) / 2 + 1);
    EXPECT_EQ(ptr, nullptr);
}

/* ═══ 资源追踪集成测试 ═══ */

TEST(KernelKmallocTest, AllocatedMemoryTrackedToTask)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->resource_head, nullptr);

    void *ptr = kern_kmalloc(128);
    ASSERT_NE(ptr, nullptr);

    /* 分配后任务应有资源追踪记录 */
    EXPECT_NE(task->resource_head, nullptr);

    kern_kfree(ptr);

    /* 释放后资源追踪应清除 */
    EXPECT_EQ(task->resource_head, nullptr);
}

TEST(KernelKmallocTest, MultipleAllocationsAreTracked)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    void *p1 = kern_kmalloc(32);
    void *p2 = kern_kmalloc(64);
    void *p3 = kern_kmalloc(128);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);

    /* 链表应有 3 个节点 */
    int count = 0;
    kern_resource_t *cur = task->resource_head;
    while (cur != NULL) {
        EXPECT_EQ(cur->type, KERN_RES_MEMORY);
        count++;
        cur = cur->next;
    }
    EXPECT_EQ(count, 3);

    kern_kfree(p1);
    kern_kfree(p2);
    kern_kfree(p3);
    EXPECT_EQ(task->resource_head, nullptr);
}

/* ═══ realloc 测试 ═══ */

TEST(KernelKmallocTest, KreallocNullBehavesLikeMalloc)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_krealloc(NULL, 128);
    ASSERT_NE(ptr, nullptr);
    kern_kfree(ptr);
}

TEST(KernelKmallocTest, KreallocZeroSizeBehavesLikeFree)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kmalloc(128);
    ASSERT_NE(ptr, nullptr);

    void *result = kern_krealloc(ptr, 0);
    EXPECT_EQ(result, nullptr);
    /* ptr 应已释放，任务资源链表为空 */
    EXPECT_EQ(kern_task_current()->resource_head, nullptr);
}

TEST(KernelKmallocTest, KreallocGrow)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kmalloc(32);
    ASSERT_NE(ptr, nullptr);

    /* 写入旧数据 */
    memset(ptr, 0xAB, 32);

    void *new_ptr = kern_krealloc(ptr, 128);
    ASSERT_NE(new_ptr, nullptr);

    /* 前 32 字节应保留 */
    uint8_t *bytes = (uint8_t *)new_ptr;
    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(bytes[i], 0xAB) << "Byte " << i << " lost in realloc";
    }

    kern_kfree(new_ptr);
}

/* ═══ 便捷宏测试 ═══ */

TEST(KernelKmallocTest, KmallocMacrosWork)
{
    kern_init();
    kern_sched_init();

    void *ptr = kmalloc(64);
    ASSERT_NE(ptr, nullptr);
    kfree(ptr);
    /* 不应崩溃 */
}

/* ═══ 边界条件测试 ═══ */

TEST(KernelKmallocTest, LargeAllocation)
{
    kern_init();
    kern_sched_init();

    void *ptr = kern_kmalloc(1024 * 1024);  /* 1 MB */
    ASSERT_NE(ptr, nullptr);
    kern_kfree(ptr);
}

