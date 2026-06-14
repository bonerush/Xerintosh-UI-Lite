/**
 * @file   test_kernel_stack.cpp
 * @brief  Xeros 任务栈管理单元测试
 * @details 测试栈使用量查询、金丝雀写入验证、空指针安全。
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
#include "kernel/kern_init.h"
}

/* ═══ 辅助任务入口 ═══ */

static void noop_task(void *arg)
{
    (void)arg;
    kern_exit();
}

/* ═══ 栈使用量查询测试 ═══ */

TEST(KernelStackTest, StackUsageReturnsZeroForNull)
{
    size_t usage = kern_task_stack_usage(NULL);
    EXPECT_EQ(usage, (size_t)0);
}

TEST(KernelStackTest, StackUsageReturnsZeroForTaskWithoutStack)
{
    kern_sched_init();

    /* 注册虚任务（无栈） */
    kern_pid_t pid = kern_task_register_virtual("nostack");
    ASSERT_GE(pid, 0);

    kern_task_t *vtask = kern_task_get(pid);
    ASSERT_NE(vtask, nullptr);
    EXPECT_EQ(kern_task_stack_usage(vtask), (size_t)0);

    /* 清理 */
    kern_task_unregister_virtual(pid);
}

TEST(KernelStackTest, StackUsageIsPositiveForSpawnedTask)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("stack_test", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    ASSERT_NE(task->stack_base, nullptr);
    EXPECT_GT(task->stack_size, (size_t)0);

    size_t usage = kern_task_stack_usage(task);
    /* 栈使用量应在合理范围内 */
    EXPECT_GT(usage, (size_t)0);
    EXPECT_LE(usage, task->stack_size);
}

TEST(KernelStackTest, StackUsageDoesNotExceedStackSize)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("limit_test", noop_task, NULL, 512);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);

    size_t usage = kern_task_stack_usage(task);
    EXPECT_LE(usage, task->stack_size);
}

/* ═══ 金丝雀测试 ═══ */

TEST(KernelStackTest, CanaryIsWrittenOnSpawn)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("canary", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    ASSERT_NE(task->stack_base, nullptr);
    ASSERT_GE(task->stack_size, (size_t)sizeof(uint32_t));

    /* 验证栈基址前 4 字节为金丝雀值 */
    uint32_t canary;
    memcpy(&canary, task->stack_base, sizeof(uint32_t));
    EXPECT_EQ(canary, (uint32_t)KERN_STACK_CANARY);
}

TEST(KernelStackTest, CanaryPersistsAfterTaskWritesCanaryAgain)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("recanary", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    ASSERT_NE(task->stack_base, nullptr);

    /* 再次写入金丝雀（幂等验证） */
    task_write_canary(task);

    uint32_t canary;
    memcpy(&canary, task->stack_base, sizeof(uint32_t));
    EXPECT_EQ(canary, (uint32_t)KERN_STACK_CANARY);
}

TEST(KernelStackTest, CanaryNotWrittenForNullTask)
{
    /* task_write_canary(NULL) 不应崩溃 */
    task_write_canary(NULL);
    SUCCEED();
}

/* ═══ 资源追踪测试 ═══ */

TEST(KernelStackTest, StackTrackedToOwnerTask)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("tracked_stack", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    ASSERT_NE(task->stack_base, nullptr);

    bool found = false;
    for (kern_resource_t *res = task->resource_head; res != NULL; res = res->next) {
        if (res->type == KERN_RES_MEMORY && res->ptr == task->stack_base) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "子任务 resource_head 中未找到指向 stack_base 的 KERN_RES_MEMORY 记录";
}

TEST(KernelStackTest, StackFreedOnTaskExit)
{
    kern_sched_init();

    for (int round = 0; round < 50; round++) {
        kern_pid_t pid = kern_spawn("stack_exit", noop_task, NULL, 0);
        ASSERT_GE(pid, 0);

        for (int i = 0; i < 100 && kern_task_get(pid) != NULL; i++) {
            kern_sched_tick();
        }

        EXPECT_EQ(kern_task_get(pid), nullptr)
            << "第 " << round << " 轮子任务未在调度后被回收";
    }
}
