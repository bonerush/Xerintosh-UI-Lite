#include <gtest/gtest.h>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_timer.h"
#include "kernel/kern_init.h"
}

static volatile int g_timer_fired = 0;

static void timer_cb(void *arg)
{
    (void)arg;
    g_timer_fired++;
}

TEST(KernelTimerTest, OneShotTimerFires)
{
    kern_init();
    kern_sched_init();
    g_timer_fired = 0;

    kern_timer_t timer;
    kern_timer_create(&timer, "oneshot", timer_cb, NULL, 5, KERN_TIMER_ONCE);
    kern_timer_start(&timer);

    for (int i = 0; i < 50 && g_timer_fired == 0; i++) {
        kern_sched_tick();
    }

    EXPECT_EQ(g_timer_fired, 1);
}

TEST(KernelTimerTest, AutoReloadTimerFiresMultiple)
{
    kern_init();
    kern_sched_init();
    g_timer_fired = 0;

    kern_timer_t timer;
    kern_timer_create(&timer, "reload", timer_cb, NULL, 3, KERN_TIMER_AUTORELOAD);
    kern_timer_start(&timer);

    for (int i = 0; i < 100 && g_timer_fired < 3; i++) {
        kern_sched_tick();
    }

    EXPECT_GE(g_timer_fired, 3);
    kern_timer_stop(&timer);
}
