#include <gtest/gtest.h>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_task_notify.h"
#include "kernel/kern_init.h"
}

TEST(KernelTaskNotifyTest, GiveSetsReceivedAndIncrementsValue)
{
    kern_init();
    kern_sched_init();

    kern_task_t *cur = kern_task_current();
    ASSERT_NE(cur, nullptr);
    cur->notify_state = KERN_NOTIFY_WAITING;

    EXPECT_EQ(kern_task_notify_give(cur), KERN_OK);
    EXPECT_EQ(cur->notify_state, KERN_NOTIFY_RECEIVED);
    EXPECT_EQ(cur->notify_value, 1u);
}

TEST(KernelTaskNotifyTest, TakeWithTimeoutZeroFailsWhenEmpty)
{
    kern_init();
    kern_sched_init();

    kern_task_t *cur = kern_task_current();
    cur->notify_value = 0;
    cur->notify_state = KERN_NOTIFY_NOT_WAITING;

    EXPECT_FALSE(kern_task_notify_take(true, 0));
}

TEST(KernelTaskNotifyTest, SetBitsAndWaitBits)
{
    kern_init();
    kern_sched_init();

    kern_task_t *cur = kern_task_current();
    cur->notify_value = 0;

    EXPECT_EQ(kern_task_notify(cur, 0x05, KERN_NOTIFY_SET_BITS), KERN_OK);
    EXPECT_EQ(kern_task_notify_wait_bits(0x04, true, false, 0), 0x05u);
    EXPECT_EQ(cur->notify_value, 0x01u);
}

static volatile bool g_notify_done = false;

static void notify_receiver(void *arg)
{
    (void)arg;
    g_notify_done = kern_task_notify_take(true, 100);
    kern_exit();
}

TEST(KernelTaskNotifyTest, GiveWakesSleepingReceiver)
{
    kern_init();
    kern_sched_init();
    g_notify_done = false;

    kern_pid_t pid = kern_spawn("notify_recv", notify_receiver, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *recv = kern_task_get(pid);
    ASSERT_NE(recv, nullptr);

    for (int i = 0; i < 20 && recv->notify_state != KERN_NOTIFY_WAITING; i++) {
        kern_sched_tick();
    }

    EXPECT_EQ(kern_task_notify_give(recv), KERN_OK);

    for (int i = 0; i < 200 && !g_notify_done; i++) {
        kern_sched_tick();
    }

    EXPECT_TRUE(g_notify_done);
}
