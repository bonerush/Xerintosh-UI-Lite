/**
 * @file   test_kernel_virtual.cpp
 * @brief  Xeros 虚任务管理单元测试
 * @details 测试虚任务注册/注销、链表可见性、调度排除、
 *          生命周期边界条件。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_init.h"
}

/* ═══ 虚任务注册测试 ═══ */

TEST(KernelVirtualTaskTest, RegisterReturnsValidPid)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("vtest");
    EXPECT_GE(pid, 0);

    /* 清理 */
    kern_task_unregister_virtual(pid);
}

TEST(KernelVirtualTaskTest, RegisterNullNameUsesDefault)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual(NULL);
    EXPECT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    /* 默认名称应包含 vtask_ 前缀 */
    EXPECT_NE(task->name[0], '\0');

    kern_task_unregister_virtual(pid);
}

TEST(KernelVirtualTaskTest, RegisterSetsVirtualFlag)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("flagged");
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_NE(task->flags & KERN_TASK_FLAG_VIRTUAL, 0);

    kern_task_unregister_virtual(pid);
}

/* ═══ 虚任务链表可见性测试 ═══ */

TEST(KernelVirtualTaskTest, AppearsInTaskList)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("visible");
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->pid, pid);
    EXPECT_STREQ(task->name, "visible");
    EXPECT_NE(task->flags & KERN_TASK_FLAG_VIRTUAL, 0);

    kern_task_unregister_virtual(pid);
}

TEST(KernelVirtualTaskTest, AppearsInListHeadChain)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("chained");
    ASSERT_GE(pid, 0);

    /* 遍历链表查找该虚任务 */
    bool found = false;
    kern_task_t *t = kern_task_list_head();
    while (t != NULL) {
        if (t->pid == pid) {
            found = true;
            break;
        }
        t = t->next;
    }
    EXPECT_TRUE(found);

    kern_task_unregister_virtual(pid);
}

/* ═══ 虚任务注销测试 ═══ */

TEST(KernelVirtualTaskTest, UnregisterRemovesFromList)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("removable");
    ASSERT_GE(pid, 0);

    kern_task_unregister_virtual(pid);

    /* 注销后不应再查找到 */
    kern_task_t *task = kern_task_get(pid);
    EXPECT_EQ(task, nullptr);
}

TEST(KernelVirtualTaskTest, UnregisterNonexistentDoesNotCrash)
{
    kern_sched_init();
    /* 注销不存在的虚任务不应崩溃 */
    kern_task_unregister_virtual(9999);
    SUCCEED();
}

/* ═══ 虚任务调度排除测试 ═══ */

TEST(KernelVirtualTaskTest, VirtualTaskNotPickedByPickNextReady)
{
    kern_sched_init();
    /* 确保 idle 就绪 */
    ASSERT_NE(g_idle_task, nullptr);
    g_idle_task->state = KERN_TASK_READY;

    kern_pid_t pid = kern_task_register_virtual("nosched");
    ASSERT_GE(pid, 0);

    /* pick_next_ready 不应选中虚任务（虚任务状态为 RUNNING，非 READY） */
    kern_task_t *next = pick_next_ready();
    ASSERT_NE(next, nullptr);
    /* 虚任务不应被选中 — pid 应与虚任务不同 */
    EXPECT_NE(next->pid, pid);

    kern_task_unregister_virtual(pid);
}

/* ═══ 虚任务 kill 交叉测试 ═══ */

TEST(KernelVirtualTaskTest, KillVirtualTaskRemovesFromList)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("killable_v");
    ASSERT_GE(pid, 0);

    int ret = kern_task_kill(pid);
    EXPECT_EQ(ret, 0);

    /* kill 虚任务后应从链表中移除 */
    kern_task_t *task = kern_task_get(pid);
    EXPECT_EQ(task, nullptr);
}

TEST(KernelVirtualTaskTest, KillNonexistentPidReturnsENOENT)
{
    kern_sched_init();
    /* 尝试 kill 一个从未注册过的 PID */
    int ret = kern_task_kill(9999);
    EXPECT_EQ(ret, KERN_ENOENT);
}

TEST(KernelVirtualTaskTest, KillAfterUnregisterReturnsENOENT)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("gone");
    ASSERT_GE(pid, 0);

    kern_task_unregister_virtual(pid);

    /* 注销后再 kill 应返回 ENOENT */
    int ret = kern_task_kill(pid);
    EXPECT_EQ(ret, KERN_ENOENT);
}

/* ═══ 虚任务状态测试 ═══ */

TEST(KernelVirtualTaskTest, VirtualTaskCreatedAsRunning)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("running_v");
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->state, KERN_TASK_RUNNING);

    kern_task_unregister_virtual(pid);
}

TEST(KernelVirtualTaskTest, VirtualTaskHasNoStack)
{
    kern_sched_init();
    kern_pid_t pid = kern_task_register_virtual("stackless");
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->stack_base, nullptr);
    EXPECT_EQ(task->stack_size, (size_t)0);

    kern_task_unregister_virtual(pid);
}
