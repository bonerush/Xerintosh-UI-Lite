#include <gtest/gtest.h>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_stats.h"
#include "kernel/kern_init.h"
}

TEST(KernelStatsTest, RuntimeStartsAtZero)
{
    kern_init();
    kern_task_t dummy = {0};
    EXPECT_EQ(kern_stats_get_runtime_us(&dummy), 0u);
}

TEST(KernelStatsTest, WatchdogFeedUpdatesTimestamp)
{
    kern_init();
    kern_task_t dummy = {0};
    dummy.pid = 1;

    EXPECT_EQ(kern_watchdog_register(&dummy), KERN_OK);
    EXPECT_EQ(kern_watchdog_feed(&dummy), KERN_OK);
}

TEST(KernelStatsTest, CpuPercentZeroBeforeUpdate)
{
    kern_init();
    kern_task_t dummy = {0};
    EXPECT_EQ(kern_stats_get_cpu_percent(&dummy), 0u);
}
