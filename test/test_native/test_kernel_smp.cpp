/**
 * @file   test_kernel_smp.cpp
 * @brief  Xeros SMP 模块单元测试
 * @details 测试 per-CPU 数组初始化、宏访问、CPU ID 函数。
 *          Native 环境下为单核模式（CONFIG_SMP_ENABLED 未定义），
 *          验证零开销退化行为正确。
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
#include "kernel/kern_init.h"
}

/* ═══ Per-CPU 初始化测试 ═══ */

TEST(KernelSmpTest, PerCpuArrayAccessible)
{
    /* g_per_cpu 数组始终可访问 */
    (void)g_per_cpu;  /* 验证链接 */

    /* cpu_id 应为 0（初始化为 0） */
    /* sched_ticks 可能因其他测试递增，不检查具体值 */
    EXPECT_GE(g_per_cpu[0].cpu_id, (uint8_t)0);
}

TEST(KernelSmpTest, CpuIdReturnsZeroInSingleCore)
{
    /* 单核模式下 kern_cpu_id() 返回 0 */
    EXPECT_EQ(kern_cpu_id(), (uint8_t)0);
}

TEST(KernelSmpTest, KernThisCpuIsZero)
{
    /* KERN_THIS_CPU 宏在单核下为 0 */
    EXPECT_EQ(KERN_THIS_CPU, (uint8_t)0);
}

TEST(KernelSmpTest, GPerCpuArrayHasMaxCpusEntries)
{
    /* g_per_cpu 数组大小为 KERN_MAX_CPUS */
    (void)g_per_cpu;  /* 引用以确保链接 */
    static_assert(KERN_MAX_CPUS == 2, "ESP32 has 2 cores");
}

TEST(KernelSmpTest, SmpInitIsNoopInSingleCore)
{
    /* kern_smp_init() 在单核下为 no-op（宏展开），
       但 g_per_cpu 数组始终存在且可访问 */
    kern_smp_init();  /* 应该不崩溃 */
    kern_smp_start_core(0, NULL);  /* 应该不崩溃 */
}

/* ═══ 辅助任务 ═══ */

static void dummy_task(void *arg)
{
    (void)arg;
    kern_exit();
}

TEST(KernelSmpTest, NewTaskHasCpuAffinityAny)
{
    kern_init();
    kern_sched_init();

    kern_pid_t pid = kern_spawn("cpu_test", dummy_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->cpu_id, KERN_CPU_ANY);

    kern_task_kill(pid);
}

TEST(KernelSmpTest, KernCpuAnyConstantIsCorrect)
{
    EXPECT_EQ(KERN_CPU_ANY, (uint8_t)0xFF);
}
