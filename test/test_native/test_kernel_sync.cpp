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
    spinlock_t lock;
    spinlock_init(&lock);
    EXPECT_FALSE(lock.locked);
}

TEST(KernelSyncTest, SpinlockLockUnlockDoesNotCrash)
{
    spinlock_t lock;
    spinlock_init(&lock);

    spinlock_lock(&lock);
    /* 单核下 spinlock_lock 为 no-op，不会自旋 */
    spinlock_unlock(&lock);

    /* 无论如何不应崩溃 */
    EXPECT_FALSE(lock.locked);
}

TEST(KernelSyncTest, SpinlockReentrantDoesNotCrash)
{
    spinlock_t lock;
    spinlock_init(&lock);

    spinlock_lock(&lock);
    spinlock_lock(&lock);  /* 重入不应崩溃 */
    spinlock_unlock(&lock);
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

TEST(KernelSyncTest, MutexUnlockWithoutLockDoesNotCrash)
{
    mutex_t m;
    mutex_init(&m);

    /* 单核下 mutex_unlock 仅设置 owner=NULL，无竞争 */
    mutex_unlock(&m);
    EXPECT_EQ(m.owner, nullptr);
}

TEST(KernelSyncTest, MutexLockAfterUnlock)
{
    kern_init();
    kern_sched_init();

    mutex_t m;
    mutex_init(&m);

    mutex_lock(&m);
    EXPECT_EQ(m.owner, g_current_task);

    mutex_unlock(&m);
    EXPECT_EQ(m.owner, nullptr);

    /* 再次加锁应成功 */
    mutex_lock(&m);
    EXPECT_EQ(m.owner, g_current_task);

    mutex_unlock(&m);
}
