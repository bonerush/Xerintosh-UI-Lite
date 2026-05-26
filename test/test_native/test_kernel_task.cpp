/**
 * @file   test_kernel_task.cpp
 * @brief  Xeros 协作式调度器单元测试
 * @details 测试 spawn/yield/sleep/exit、动态栈、金丝雀检测、
 *          Round-Robin 调度。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_init.h"
}

/* ═══ 辅助：任务间通信变量 ═══ */

static volatile int g_simple_counter = 0;
static volatile int g_task_a_runs = 0;
static volatile int g_task_b_runs = 0;
static volatile bool g_long_running_done = false;

/* ═══ 简单任务入口 ═══ */

static void simple_task(void *arg)
{
    g_simple_counter++;
    kern_exit();
}

static void counter_task_a(void *arg)
{
    for (int i = 0; i < 10; i++) {
        g_task_a_runs++;
        kern_yield();
    }
    kern_exit();
}

static void counter_task_b(void *arg)
{
    for (int i = 0; i < 10; i++) {
        g_task_b_runs++;
        kern_yield();
    }
    kern_exit();
}

static void sleep_task(void *arg)
{
    /* 先让出，让其他任务运行 */
    kern_yield();
    /* 休眠 100ms */
    kern_sleep_ms(100);
    /* 醒来后标记完成 */
    g_long_running_done = true;
    kern_exit();
}

static void deep_stack_task(void *arg)
{
    /* 使用一定栈空间（递归） */
    volatile char buf[512];
    for (int i = 0; i < 512; i++) {
        buf[i] = (char)(i & 0xFF);
    }
    /* 确保 buf 被使用 */
    g_simple_counter = (int)buf[0] + (int)buf[511];
    kern_exit();
}

/* ═══ 调度器初始化测试 ═══ */

TEST(KernelTaskTest, SchedInitDoesNotCrash)
{
    kern_sched_init();
    SUCCEED();
}

TEST(KernelTaskTest, SchedInitCreatesIdleTask)
{
    kern_sched_init();
    /* 初始化后应有一个 idle 任务 */
    EXPECT_GE(kern_task_count(), 1);
}

/* ═══ 任务创建测试 ═══ */

TEST(KernelTaskTest, SpawnCreatesNewTask)
{
    kern_sched_init();
    g_simple_counter = 0;

    kern_pid_t pid = kern_spawn("simple", simple_task, NULL, 0);
    EXPECT_GE(pid, 0);
}

TEST(KernelTaskTest, SpawnRegistersTaskInList)
{
    kern_sched_init();
    uint8_t before = kern_task_count();

    kern_spawn("test", simple_task, NULL, 0);
    EXPECT_GT(kern_task_count(), before);
}

TEST(KernelTaskTest, SpawnNullNameUsesDefault)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn(NULL, simple_task, NULL, 0);
    EXPECT_GE(pid, 0);
}

TEST(KernelTaskTest, SpawnNullEntryReturnsError)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("bad", NULL, NULL, 0);
    EXPECT_EQ(pid, KERN_EINVAL);
}

TEST(KernelTaskTest, SpawnGeneratesUniquePids)
{
    kern_sched_init();
    kern_pid_t pid1 = kern_spawn("one", simple_task, NULL, 0);
    kern_pid_t pid2 = kern_spawn("two", simple_task, NULL, 0);

    EXPECT_GE(pid1, 0);
    EXPECT_GE(pid2, 0);
    EXPECT_NE(pid1, pid2);
}

/* ═══ 任务调度测试 ═══ */

TEST(KernelTaskTest, SimpleTaskRuns)
{
    kern_sched_init();
    g_simple_counter = 0;

    kern_spawn("counter", simple_task, NULL, 0);

    /* 调度直到所有任务完成 */
    for (int i = 0; i < 100 && g_simple_counter == 0; i++) {
        kern_sched_tick();
    }
    EXPECT_EQ(g_simple_counter, 1);
}

TEST(KernelTaskTest, RoundRobinAlternates)
{
    kern_sched_init();
    g_task_a_runs = 0;
    g_task_b_runs = 0;

    kern_spawn("task_a", counter_task_a, NULL, 0);
    kern_spawn("task_b", counter_task_b, NULL, 0);

    /* 调度直到所有任务完成 */
    for (int i = 0; i < 500; i++) {
        kern_sched_tick();
        if (g_task_a_runs >= 10 && g_task_b_runs >= 10) break;
    }

    EXPECT_EQ(g_task_a_runs, 10);
    EXPECT_EQ(g_task_b_runs, 10);
}

/* ═══ Sleep/Wake 测试 ═══ */

TEST(KernelTaskTest, SleepTaskWakesUp)
{
    kern_sched_init();
    g_long_running_done = false;

    kern_spawn("sleeper", sleep_task, NULL, 0);

    /* 调度足够长时间 */
    for (int i = 0; i < 500; i++) {
        kern_sched_tick();
        if (g_long_running_done) break;
    }

    EXPECT_TRUE(g_long_running_done);
}

/* ═══ 任务查询测试 ═══ */

TEST(KernelTaskTest, TaskGetByIdReturnsValidTask)
{
    kern_sched_init();
    kern_pid_t pid = kern_spawn("findable", simple_task, NULL, 0);

    kern_task_t *t = kern_task_get(pid);
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(t->name, "findable");
    EXPECT_EQ(t->pid, pid);
}

TEST(KernelTaskTest, TaskGetByInvalidPidReturnsNull)
{
    kern_sched_init();
    kern_task_t *t = kern_task_get(999);
    EXPECT_EQ(t, nullptr);
}

TEST(KernelTaskTest, TaskCurrentIsNotNull)
{
    kern_sched_init();
    kern_task_t *cur = kern_task_current();
    ASSERT_NE(cur, nullptr);
}

TEST(KernelTaskTest, TaskListHeadIsNotNull)
{
    kern_sched_init();
    kern_task_t *head = kern_task_list_head();
    ASSERT_NE(head, nullptr);
}

/* ═══ 栈管理测试 ═══ */

TEST(KernelTaskTest, CurrentTaskHasAllocatedStack)
{
    kern_sched_init();
    kern_spawn("stack_test", simple_task, NULL, 0);

    kern_task_t *cur = kern_task_current();
    ASSERT_NE(cur, nullptr);
    EXPECT_GT(cur->stack_size, 0);
    EXPECT_NE(cur->stack_base, nullptr);
}

TEST(KernelTaskTest, StackUsageIsPlausible)
{
    kern_sched_init();
    kern_spawn("usage_test", simple_task, NULL, 0);

    kern_task_t *cur = kern_task_current();
    size_t usage = kern_task_stack_usage(cur);
    /* 栈使用量应该在 (0, stack_size] 之间 */
    EXPECT_GT(usage, 0);
    EXPECT_LE(usage, cur->stack_size);
}

TEST(KernelTaskTest, DeepStackTaskDoesNotOverflow)
{
    kern_sched_init();
    g_simple_counter = 0;

    kern_pid_t pid = kern_spawn("deep", deep_stack_task, NULL, 1024);
    EXPECT_GE(pid, 0);

    for (int i = 0; i < 100; i++) {
        kern_sched_tick();
        if (kern_task_get(pid) == NULL) break;
    }
    /* 任务应该正常完成，不触发 panic */
    SUCCEED();
}

/* ═══ 金丝雀测试 ═══ */

TEST(KernelTaskTest, StackCanaryIsSet)
{
    kern_sched_init();
    kern_spawn("canary_test", simple_task, NULL, 0);

    kern_task_t *cur = kern_task_current();
    uint32_t canary = kern_task_stack_canary(cur);
    EXPECT_EQ(canary, KERN_STACK_CANARY);
}
