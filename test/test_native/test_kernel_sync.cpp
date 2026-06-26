/**
 * @file   test_kernel_sync.cpp
 * @brief  Xeros 同步原语单元测试
 * @details 测试 spinlock_init/lock/unlock 和 mutex_init/lock/unlock。
 *          Native 环境为单核模式，spinlock 退化为空操作，
 *          mutex 退化为简单所有者标记。验证接口不崩溃且语义正确。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_smp.h"
#include "kernel/kern_sync.h"
#include "kernel/kern_init.h"
}

/* ═══ 自旋锁测试 ═══ */

TEST(KernelSyncTest, SpinlockInitDoesNotCrash)
{
    xeros_spinlock_t lock;
    xeros_spinlock_init(&lock);
    EXPECT_FALSE(lock.locked);
}

TEST(KernelSyncTest, SpinlockLockUnlockDoesNotCrash)
{
    xeros_spinlock_t lock;
    xeros_spinlock_init(&lock);

    xeros_spinlock_lock(&lock);
    /* 单核下 spinlock_lock 为 no-op，不会自旋 */
    xeros_spinlock_unlock(&lock);

    /* 无论如何不应崩溃 */
    EXPECT_FALSE(lock.locked);
}

TEST(KernelSyncTest, SpinlockReentrantDoesNotCrash)
{
    xeros_spinlock_t lock;
    xeros_spinlock_init(&lock);

    xeros_spinlock_lock(&lock);
    xeros_spinlock_lock(&lock);  /* 重入不应崩溃 */
    xeros_spinlock_unlock(&lock);
}

/* ═══ 互斥锁测试 ═══ */

TEST(KernelSyncTest, MutexInitClearsFields)
{
    mutex_t m;
    mutex_init(&m);

    EXPECT_EQ(m.owner, nullptr);
    EXPECT_EQ(m.wait_queue, nullptr);
}

TEST(KernelSyncTest, MutexLockSetsOwner)
{
    kern_init();
    kern_sched_init();

    mutex_t m;
    mutex_init(&m);

    mutex_lock(&m);
    EXPECT_EQ(m.owner, g_current_task);

    mutex_unlock(&m);
    EXPECT_EQ(m.owner, nullptr);
}

TEST(KernelSyncTest, MutexUnlockWithoutLockReturnsPermError)
{
    kern_init();
    kern_sched_init();

    mutex_t m;
    mutex_init(&m);

    /* 未持有锁时解锁应返回 KERN_EPERM */
    EXPECT_EQ(mutex_unlock(&m), KERN_EPERM);
    EXPECT_EQ(m.owner, nullptr);
}

TEST(KernelSyncTest, MutexLockAfterUnlock)
{
    kern_init();
    kern_sched_init();

    mutex_t m;
    mutex_init(&m);

    EXPECT_EQ(mutex_lock(&m), KERN_OK);
    EXPECT_EQ(m.owner, g_current_task);

    EXPECT_EQ(mutex_unlock(&m), KERN_OK);
    EXPECT_EQ(m.owner, nullptr);

    /* 再次加锁应成功 */
    EXPECT_EQ(mutex_lock(&m), KERN_OK);
    EXPECT_EQ(m.owner, g_current_task);

    EXPECT_EQ(mutex_unlock(&m), KERN_OK);
}

TEST(KernelSyncTest, MutexRecursiveLockIncrementsCount)
{
    kern_init();
    kern_sched_init();

    mutex_t m;
    mutex_init(&m);

    EXPECT_EQ(mutex_lock(&m), KERN_OK);
    EXPECT_EQ(m.owner, g_current_task);
    EXPECT_EQ(m.recursive_count, 1u);

    EXPECT_EQ(mutex_lock(&m), KERN_OK);   /* 递归加锁 */
    EXPECT_EQ(m.recursive_count, 2u);

    EXPECT_EQ(mutex_unlock(&m), KERN_OK); /* 第一次解锁不释放 */
    EXPECT_EQ(m.owner, g_current_task);
    EXPECT_EQ(m.recursive_count, 1u);

    EXPECT_EQ(mutex_unlock(&m), KERN_OK); /* 最终释放 */
    EXPECT_EQ(m.owner, nullptr);
    EXPECT_EQ(m.recursive_count, 0u);
}

TEST(KernelSyncTest, MutexUnlockByNonOwnerFails)
{
    kern_init();
    kern_sched_init();

    mutex_t m;
    mutex_init(&m);
    EXPECT_EQ(mutex_lock(&m), KERN_OK);

    /* 伪造非 owner 任务指针 */
    kern_task_t fake = {0};
    kern_task_t *real = g_current_task;
    g_current_task = &fake;

    kern_err_t rc = mutex_unlock(&m);
    g_current_task = real;

    EXPECT_EQ(rc, KERN_EPERM);
    EXPECT_EQ(m.owner, real);
}
