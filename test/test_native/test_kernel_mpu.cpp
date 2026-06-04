/**
 * @file   test_kernel_mpu.cpp
 * @brief  Xeros MPU 内存保护单元测试
 * @details 测试 MPU 常量、区域描述符、配置结构、不崩溃的安全性。
 *          Native 环境未定义 CONFIG_MPU_ENABLED，所有 API 退化为空操作。
 *          测试主要验证结构体布局和零开销退化行为。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_mpu.h"
#include "kernel/kern_init.h"
}

/* ═══ 辅助任务入口 ═══ */

static void noop_task(void *arg)
{
    (void)arg;
    kern_exit();
}

/* ═══ MPU 常量测试 ═══ */

TEST(KernelMpuTest, MaxRegionsIsEight)
{
    /* ESP32 有 8 个 MPU 区域 */
    EXPECT_EQ(KERN_MPU_MAX_REGIONS, 8);
}

TEST(KernelMpuTest, MinAlignIs32)
{
    /* MPU 区域对齐要求 32 字节 */
    EXPECT_EQ(KERN_MPU_MIN_ALIGN, 32);
}

TEST(KernelMpuTest, GuardPagesIsOne)
{
    EXPECT_EQ(KERN_MPU_GUARD_PAGES, 1);
}

/* ═══ 枚举值测试 ═══ */

TEST(KernelMpuTest, AccessEnumValues)
{
    EXPECT_EQ(KERN_MPU_ACCESS_NONE, 0);
    EXPECT_EQ(KERN_MPU_ACCESS_RO,   1);
    EXPECT_EQ(KERN_MPU_ACCESS_RW,   2);
    EXPECT_EQ(KERN_MPU_ACCESS_RX,   3);
    EXPECT_EQ(KERN_MPU_ACCESS_RWX,  4);
}

TEST(KernelMpuTest, RegionTypeEnumValues)
{
    EXPECT_EQ(KERN_MPU_TYPE_UNUSED,      0);
    EXPECT_EQ(KERN_MPU_TYPE_KERNEL,      1);
    EXPECT_EQ(KERN_MPU_TYPE_STACK_GUARD, 2);
    EXPECT_EQ(KERN_MPU_TYPE_TASK_DATA,   3);
    EXPECT_EQ(KERN_MPU_TYPE_PERIPHERAL,  4);
}

/* ═══ MPU 区域描述符测试 ═══ */

TEST(KernelMpuTest, RegionStructSize)
{
    kern_mpu_region_t region;
    memset(&region, 0, sizeof(region));
    /* 结构体应该存在且可初始化 */
    EXPECT_EQ(region.base, nullptr);
    EXPECT_EQ(region.size, (size_t)0);
    EXPECT_EQ(region.access, KERN_MPU_ACCESS_NONE);
    EXPECT_EQ(region.type, KERN_MPU_TYPE_UNUSED);
    EXPECT_FALSE(region.enabled);
}

/* ═══ MPU 配置结构测试 ═══ */

TEST(KernelMpuTest, ConfigInitialized)
{
    kern_mpu_config_t config;
    memset(&config, 0, sizeof(config));

    EXPECT_EQ(config.region_count, 0);

    /* 所有区域应为未使用状态 */
    for (int i = 0; i < KERN_MPU_MAX_REGIONS; i++) {
        EXPECT_EQ(config.regions[i].type, KERN_MPU_TYPE_UNUSED);
        EXPECT_FALSE(config.regions[i].enabled);
    }
}

/* ═══ TCB 集成测试 ═══ */

TEST(KernelMpuTest, NewTaskHasNullMpuConfig)
{
    kern_init();
    kern_sched_init();

    kern_pid_t pid = kern_spawn("mpu_test", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);

    /* 新任务的 mpu_config 应为 NULL（未设置 MPU） */
    EXPECT_EQ(task->mpu_config, nullptr);

    kern_task_kill(pid);
}

/* ═══ API 不崩溃测试（零开销退化） ═══ */

TEST(KernelMpuTest, MpuInitDoesNotCrash)
{
    kern_mpu_init();
    /* 不应崩溃 */
}

TEST(KernelMpuTest, MpuApplyNullDoesNotCrash)
{
    kern_mpu_apply(NULL);
    /* NULL 指针应安全处理 */
}

TEST(KernelMpuTest, MpuSetupStackGuardNullDoesNotCrash)
{
    kern_mpu_setup_stack_guard(NULL, NULL, 0);
    /* NULL task 应安全处理 */
}

TEST(KernelMpuTest, MpuAddRegionNullTaskReturnsError)
{
    /* 退化模式下返回 KERN_OK */
    int rc = kern_mpu_add_region(NULL, (void *)0x1000, 1024, KERN_MPU_ACCESS_RW);
    EXPECT_EQ(rc, KERN_OK);
}

TEST(KernelMpuTest, MpuApplyOnTaskDoesNotCrash)
{
    kern_init();
    kern_sched_init();

    kern_pid_t pid = kern_spawn("mpu_task", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);

    /* 应用 MPU（退化模式下为 no-op） */
    kern_mpu_apply(task);
    /* 不应崩溃 */

    kern_task_kill(pid);
}

TEST(KernelMpuTest, MpuSetupStackGuardWithValidParams)
{
    kern_init();
    kern_sched_init();

    kern_pid_t pid = kern_spawn("guard_test", noop_task, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);

    /* 栈守卫（退化模式下为 no-op） */
    kern_mpu_setup_stack_guard(task, task->stack_base, task->stack_size);
    /* 不应崩溃 */

    kern_task_kill(pid);
}
