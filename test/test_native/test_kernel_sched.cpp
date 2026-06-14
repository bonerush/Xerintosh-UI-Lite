/**
 * @file   test_kernel_sched.cpp
 * @brief  Xeros 调度器核心单元测试
 * @details 测试调度器初始化、pick_next_ready 选择逻辑、
 *          tick 计数器、Round-Robin 交替调度。
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

/* ═══ 辅助：任务间通信变量 ═══ */

static volatile int g_sched_counter = 0;
static volatile int g_rr_a_count = 0;
static volatile int g_rr_b_count = 0;

/* ═══ 辅助任务入口 ═══ */

static void simple_counter(void *arg)
{
    (void)arg;
    g_sched_counter++;
    kern_exit();
}

static void rr_task_a(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) {
        g_rr_a_count++;
        kern_yield();
    }
    kern_exit();
}

static void rr_task_b(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) {
        g_rr_b_count++;
        kern_yield();
    }
    kern_exit();
}

/* ═══ 调度器初始化测试 ═══ */

TEST(KernelSchedTest, InitCreatesIdleTask)
{
    kern_sched_init();
    ASSERT_NE(g_idle_task, nullptr);
    EXPECT_EQ(g_idle_task->pid, 0);
    EXPECT_STREQ(g_idle_task->name, "idle");
    EXPECT_EQ(g_idle_task->priority, 0);
}

TEST(KernelSchedTest, InitSetsIdleReady)
{
    kern_sched_init();
    ASSERT_NE(g_idle_task, nullptr);
    EXPECT_EQ(g_idle_task->state, KERN_TASK_READY);
}

/* ═══ pick_next_ready 测试 ═══ */

TEST(KernelSchedTest, PickNextReadyReturnsIdleWhenAlone)
{
    kern_sched_init();
    kern_task_t *next = pick_next_ready();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->pid, 0);
    EXPECT_STREQ(next->name, "idle");
}

TEST(KernelSchedTest, PickNextReadyReturnsNullWhenNoReadyTasks)
{
    kern_sched_init();
    /* 将 idle 设为 SLEEPING，模拟无就绪任务场景。
     * 需将 wake_time 设为远大于 g_sched_ticks 的值，
     * 避免 pick_next_ready 的唤醒循环将其恢复为 READY。 */
    g_idle_task->wake_time = g_sched_ticks + 10000;
    g_idle_task->state = KERN_TASK_SLEEPING;
    kern_task_t *next = pick_next_ready();
    EXPECT_EQ(next, nullptr);
    /* 恢复状态 */
    g_idle_task->state = KERN_TASK_READY;
}

/* ═══ Tick 测试 ═══ */

TEST(KernelSchedTest, TickIncrementsCounter)
{
    kern_sched_init();
    uint32_t before = g_sched_ticks;
    kern_sched_tick();
    EXPECT_GT(g_sched_ticks, before);
}

TEST(KernelSchedTest, TickRunsMultipleTimes)
{
    kern_sched_init();
    kern_sched_tick();
    uint32_t after_one = g_sched_ticks;
    kern_sched_tick();
    EXPECT_GT(g_sched_ticks, after_one);
}

/* ═══ 任务调度测试 ═══ */

TEST(KernelSchedTest, SpawnedTaskIsPickedByPickNextReady)
{
    kern_sched_init();
    g_sched_counter = 0;

    kern_pid_t pid = kern_spawn("pick_test", simple_counter, NULL, 0);
    ASSERT_GE(pid, 0);

    /* pick_next_ready 应返回新任务（非 idle） */
    kern_task_t *next = pick_next_ready();
    ASSERT_NE(next, nullptr);
    EXPECT_NE(next, g_idle_task);
    EXPECT_EQ(next->pid, pid);
}

TEST(KernelSchedTest, PickNextReadyReturnsIdleAfterTaskExit)
{
    kern_sched_init();
    g_sched_counter = 0;

    kern_pid_t pid = kern_spawn("exit_test", simple_counter, NULL, 0);
    ASSERT_GE(pid, 0);

    /* 运行任务直到完成 */
    for (int i = 0; i < 100 && g_sched_counter == 0; i++) {
        kern_sched_tick();
    }
    EXPECT_EQ(g_sched_counter, 1);

    /* 任务退出后，pick_next_ready 应返回 idle */
    kern_task_t *next = pick_next_ready();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next, g_idle_task);

    /* 退出后的任务状态应为 ZOMBIE */
    kern_task_t *task = kern_task_get(pid);
    if (task != nullptr) {
        EXPECT_EQ(task->state, KERN_TASK_ZOMBIE);
    }
}

/* ═══ Round-Robin 交替调度测试 ═══ */

TEST(KernelSchedTest, RoundRobinAlternatesBetweenTwoTasks)
{
    kern_sched_init();
    g_rr_a_count = 0;
    g_rr_b_count = 0;

    kern_spawn("rr_a", rr_task_a, NULL, 0);
    kern_spawn("rr_b", rr_task_b, NULL, 0);

    /* 调度直到所有任务完成 */
    for (int i = 0; i < 500; i++) {
        kern_sched_tick();
        if (g_rr_a_count >= 5 && g_rr_b_count >= 5) break;
    }

    EXPECT_EQ(g_rr_a_count, 5);
    EXPECT_EQ(g_rr_b_count, 5);
}

/* ═══ 调度类注册 API 返回类型测试 ═══ */

TEST(KernelSchedTest, SchedClassRegisterReturnsEinval)
{
    kern_err_t rc = kern_sched_class_register(NULL);
    EXPECT_EQ(rc, KERN_EINVAL);
}

TEST(KernelSchedTest, SchedClassRegisterReturnsEnospc)
{
    kern_sched_init();

    /* 保存并恢复 class 注册表，避免栈上 dummy 指针悬空影响后续测试 */
    uint8_t saved_count = g_sched_class_count;
    kern_sched_class_t *saved_classes[KERN_SCHED_MAX_CLASSES];
    memcpy(saved_classes, g_sched_classes, sizeof(saved_classes));

    kern_sched_class_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    /* 填满剩余注册槽 */
    while (g_sched_class_count < KERN_SCHED_MAX_CLASSES) {
        kern_err_t rc = kern_sched_class_register(&dummy);
        EXPECT_EQ(rc, KERN_OK);
    }

    /* 超过最大数量时应返回 KERN_ENOSPC */
    kern_err_t rc = kern_sched_class_register(&dummy);
    EXPECT_EQ(rc, KERN_ENOSPC);

    /* 恢复 class 注册表 */
    g_sched_class_count = saved_count;
    memcpy(g_sched_classes, saved_classes, KERN_SCHED_MAX_CLASSES * sizeof(kern_sched_class_t *));
}
