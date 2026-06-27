#include <gtest/gtest.h>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_task_ctrl.h"
#include "kernel/kern_init.h"
}

TEST(KernelTaskCtrlTest, PrioritySetGet)
{
    kern_init();
    kern_sched_init();

    EXPECT_EQ(kern_task_priority_set(nullptr, 200), KERN_OK);
    EXPECT_EQ(kern_task_priority_get(nullptr), 200u);
}

TEST(KernelTaskCtrlTest, DelayUntilAdvancesWakeTimeWithoutBlocking)
{
    kern_init();
    kern_sched_init();

    uint32_t prev = g_sched_ticks - 10;
    kern_task_delay_until(&prev, 10);

    EXPECT_EQ(prev, g_sched_ticks);
}

static volatile int g_ctrl_counter = 0;

static void ctrl_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        g_ctrl_counter++;
        kern_yield();
    }
    kern_exit();
}

TEST(KernelTaskCtrlTest, SuspendStopsTask)
{
    kern_init();
    kern_sched_init();
    g_ctrl_counter = 0;

    kern_pid_t pid = kern_spawn("ctrl_worker", ctrl_worker, NULL, 0);
    ASSERT_GE(pid, 0);
    kern_task_t *task = kern_task_get(pid);

    /* 先让它跑起来 */
    for (int i = 0; i < 10 && g_ctrl_counter == 0; i++) {
        kern_sched_tick();
    }
    EXPECT_GT(g_ctrl_counter, 0);

    int seen = g_ctrl_counter;
    EXPECT_EQ(kern_task_suspend(task), KERN_OK);

    for (int i = 0; i < 50; i++) {
        kern_sched_tick();
    }

    EXPECT_EQ(g_ctrl_counter, seen);

    EXPECT_EQ(kern_task_resume(task), KERN_OK);
    for (int i = 0; i < 100 && g_ctrl_counter < 3; i++) {
        kern_sched_tick();
    }
    EXPECT_EQ(g_ctrl_counter, 3);
}
