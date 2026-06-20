/**
 * @file   test_kernel_resource.cpp
 * @brief  Xeros 资源追踪模块单元测试
 * @details 测试 kern_resource_track / untrack / release_all 的完整生命周期。
 *          验证链表的插入、删除、遍历释放行为。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_resource.h"
#include "kernel/kern_kmalloc.h"
#include "kernel/kern_init.h"
}

/* ═══ 辅助：释放计数器 ═══ */

static int g_release_count = 0;

static void counting_release(void *ptr)
{
    (void)ptr;
    g_release_count++;
}

/* ═══ 辅助任务入口 ═══ */

static void noop_task(void *arg)
{
    (void)arg;
    kern_exit();
}

/* ═══ 资源追踪测试 ═══ */

TEST(KernelResourceTest, TrackNullTaskReturnsEinval)
{
    int rc = kern_resource_track(NULL, (void *)0x1000,
                                 KERN_RES_MEMORY, counting_release);
    EXPECT_EQ(rc, KERN_EINVAL);
}

TEST(KernelResourceTest, TrackNullPtrReturnsEinval)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    int rc = kern_resource_track(task, NULL,
                                 KERN_RES_MEMORY, counting_release);
    EXPECT_EQ(rc, KERN_EINVAL);
}

TEST(KernelResourceTest, TrackNullReleaseReturnsEinval)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    int rc = kern_resource_track(task, (void *)0x1000,
                                 KERN_RES_MEMORY, NULL);
    EXPECT_EQ(rc, KERN_EINVAL);
}

TEST(KernelResourceTest, TrackAndUntrack)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    /* 隔离 idle 任务栈资源 */
    kern_resource_t *idle_stack_res = task->resource_head;
    task->resource_head = nullptr;

    void *resource = (void *)0xDEAD;
    EXPECT_EQ(task->resource_head, nullptr);

    /* 追踪 */
    int rc = kern_resource_track(task, resource,
                                 KERN_RES_MEMORY, counting_release);
    EXPECT_EQ(rc, KERN_OK);
    EXPECT_NE(task->resource_head, nullptr);

    /* 取消追踪 */
    rc = kern_resource_untrack(task, resource);
    EXPECT_EQ(rc, KERN_OK);
    EXPECT_EQ(task->resource_head, nullptr);

    /* 恢复 idle 栈资源 */
    task->resource_head = idle_stack_res;
}

TEST(KernelResourceTest, UntrackNonexistentReturnsEnoent)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    int rc = kern_resource_untrack(task, (void *)0xBEEF);
    EXPECT_EQ(rc, KERN_ENOENT);
}

TEST(KernelResourceTest, MultipleResourcesInLinkedList)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    /* 隔离 idle 任务栈资源，避免 release_all 误释放 idle 栈 */
    kern_resource_t *idle_stack_res = task->resource_head;
    task->resource_head = nullptr;

    void *r1 = (void *)0x1000;
    void *r2 = (void *)0x2000;
    void *r3 = (void *)0x3000;

    EXPECT_EQ(kern_resource_track(task, r1, KERN_RES_MEMORY, counting_release), KERN_OK);
    EXPECT_EQ(kern_resource_track(task, r2, KERN_RES_MUTEX, counting_release), KERN_OK);
    EXPECT_EQ(kern_resource_track(task, r3, KERN_RES_FD, counting_release), KERN_OK);

    /* 链表应有 3 个节点 */
    int count = 0;
    kern_resource_t *cur = task->resource_head;
    while (cur != NULL) {
        count++;
        cur = cur->next;
    }
    EXPECT_EQ(count, 3);

    /* 清理 */
    kern_resource_release_all(task);
    EXPECT_EQ(task->resource_head, nullptr);

    /* 恢复 idle 栈资源 */
    task->resource_head = idle_stack_res;
}

TEST(KernelResourceTest, ReleaseAllCallsAllCallbacks)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    /* 隔离 idle 任务栈资源，避免 release_all 误释放 idle 栈 */
    kern_resource_t *idle_stack_res = task->resource_head;
    task->resource_head = nullptr;

    g_release_count = 0;

    kern_resource_track(task, (void *)0x1000, KERN_RES_MEMORY, counting_release);
    kern_resource_track(task, (void *)0x2000, KERN_RES_MEMORY, counting_release);
    kern_resource_track(task, (void *)0x3000, KERN_RES_MEMORY, counting_release);

    kern_resource_release_all(task);

    EXPECT_EQ(g_release_count, 3);
    EXPECT_EQ(task->resource_head, nullptr);

    /* 恢复 idle 栈资源 */
    task->resource_head = idle_stack_res;
}

TEST(KernelResourceTest, ReleaseAllOnNullTaskDoesNotCrash)
{
    kern_resource_release_all(NULL);
    /* 不应崩溃 */
}

TEST(KernelResourceTest, TrackWithNullReleaseInReleaseAllSkips)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    /* 手动构造一个 release=NULL 的节点来测试边界条件 */
    kern_resource_t *res = (kern_resource_t *)kern_kmalloc_untracked(sizeof(kern_resource_t));
    ASSERT_NE(res, nullptr);
    res->ptr = (void *)0x5555;
    res->type = KERN_RES_MEMORY;
    res->release = NULL;
    res->next = NULL;

    task->resource_head = res;

    /* release_all 不应因为 NULL release 回调而崩溃 */
    kern_resource_release_all(task);
    EXPECT_EQ(task->resource_head, nullptr);
}

TEST(KernelResourceTest, ResourceLockInitialized)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    EXPECT_EQ(task->resource_lock, false);
}

TEST(KernelResourceTest, ResourceTrackUnderLock)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    /* 隔离 idle 任务栈资源，避免 release_all 误释放 idle 栈 */
    kern_resource_t *idle_stack_res = task->resource_head;
    task->resource_head = nullptr;

    void *r1 = (void *)0x1000;
    void *r2 = (void *)0x2000;
    void *r3 = (void *)0x3000;

    EXPECT_EQ(kern_resource_track(task, r1, KERN_RES_MEMORY, counting_release), KERN_OK);
    EXPECT_EQ(kern_resource_track(task, r2, KERN_RES_MUTEX, counting_release), KERN_OK);
    EXPECT_EQ(kern_resource_track(task, r3, KERN_RES_FD, counting_release), KERN_OK);

    /* 锁在每次操作后应释放 */
    EXPECT_EQ(task->resource_lock, false);

    /* 链表应有 3 个节点 */
    int count = 0;
    kern_resource_t *cur = task->resource_head;
    while (cur != NULL) {
        count++;
        cur = cur->next;
    }
    EXPECT_EQ(count, 3);

    /* 清理 */
    kern_resource_release_all(task);
    EXPECT_EQ(task->resource_head, nullptr);
    EXPECT_EQ(task->resource_lock, false);

    /* 恢复 idle 栈资源 */
    task->resource_head = idle_stack_res;
}

TEST(KernelResourceTest, PoolSizeLinkedToMaxTasks)
{
    kern_init();
    kern_sched_init();

    kern_task_t *task = kern_task_current();
    ASSERT_NE(task, nullptr);

    /* 隔离 idle 任务栈资源 */
    kern_resource_t *idle_stack_res = task->resource_head;
    task->resource_head = nullptr;

    /* 池大小 = KERN_MAX_TASKS * 4 = 64；追踪 40 个应全部成功 */
    static void *ptrs[40];
    for (int i = 0; i < 40; i++) {
        ptrs[i] = (void *)(uintptr_t)(0x1000 + (size_t)i);
        EXPECT_EQ(kern_resource_track(task, ptrs[i], KERN_RES_MEMORY, counting_release), KERN_OK)
            << "第 " << i << " 个资源追踪失败";
    }

    /* 验证链表长度 */
    int count = 0;
    for (kern_resource_t *cur = task->resource_head; cur != NULL; cur = cur->next) {
        count++;
    }
    EXPECT_EQ(count, 40);

    /* 清理 */
    kern_resource_release_all(task);
    EXPECT_EQ(task->resource_head, nullptr);

    /* 恢复 idle 栈资源 */
    task->resource_head = idle_stack_res;
}
