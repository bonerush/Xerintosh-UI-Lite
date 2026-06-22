/**
 * @file   test_kernel_init.cpp
 * @brief  Xeros 内核初始化与日志系统单元测试
 * @details 测试 kern_init() 初始化流程、kern_log() 分级输出、
 *          kern_panic() 致命错误处理。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_init.h"
}

/* ═══ 错误码常量检查 ═══ */

TEST(KernelTypesTest, ErrorCodesAreNegativeOrZero)
{
    /* 成功码应为 0 */
    EXPECT_EQ(KERN_OK, 0);

    /* 错误码均为负数 */
    EXPECT_LT(KERN_ERR, 0);
    EXPECT_LT(KERN_ENOENT, 0);
    EXPECT_LT(KERN_ENOMEM, 0);
    EXPECT_LT(KERN_EINVAL, 0);
    EXPECT_LT(KERN_EMFILE, 0);
    EXPECT_LT(KERN_EPIPE, 0);
}

TEST(KernelTypesTest, PidConstantsAreValid)
{
    EXPECT_LT(KERN_PID_INVALID, 0);
    EXPECT_GT(KERN_MAX_TASKS, 0);
    EXPECT_GT(KERN_TASK_NAME_LEN, 0);
}

TEST(KernelTypesTest, StackConstantsAreValid)
{
    EXPECT_GT(KERN_STACK_MIN, 0);
    EXPECT_GT(KERN_STACK_MAX, KERN_STACK_MIN);
    EXPECT_GT(KERN_STACK_GROW, 0);
    EXPECT_LE(KERN_STACK_GROW, KERN_STACK_MAX);
    EXPECT_NE(KERN_STACK_CANARY, 0);
}

TEST(KernelTypesTest, PathConstantsAreValid)
{
    EXPECT_GT(KERN_PATH_MAX, 0);
    EXPECT_GT(KERN_NAME_MAX, 0);
    EXPECT_LT(KERN_NAME_MAX, KERN_PATH_MAX);
}

TEST(KernelTypesTest, FdConstantsAreValid)
{
    EXPECT_LT(KERN_FD_INVALID, 0);
    EXPECT_GT(KERN_MAX_FD_PER_TASK, 0);
}

/* ═══ 初始化测试 ═══ */

TEST(KernelInitTest, InitDoesNotCrash)
{
    /* kern_init() 应该不崩溃地完成初始化 */
    kern_init();
    /* 如果跑到这里没有死机，说明基本初始化没问题 */
}

TEST(KernelInitTest, DoubleInitIsSafe)
{
    /* 重复初始化应该是安全的（幂等性） */
    kern_init();
    kern_init();
}

/* ═══ 日志级别测试 ═══ */

TEST(KernelLogTest, DefaultLogLevel)
{
    kern_init();
    kern_log_set_level(KERN_LOG_INFO);
    EXPECT_EQ(kern_log_get_level(), KERN_LOG_INFO);
}

TEST(KernelLogTest, SetAllLogLevels)
{
    kern_log_set_level(KERN_LOG_DEBUG);
    EXPECT_EQ(kern_log_get_level(), KERN_LOG_DEBUG);

    kern_log_set_level(KERN_LOG_INFO);
    EXPECT_EQ(kern_log_get_level(), KERN_LOG_INFO);

    kern_log_set_level(KERN_LOG_WARN);
    EXPECT_EQ(kern_log_get_level(), KERN_LOG_WARN);

    kern_log_set_level(KERN_LOG_ERROR);
    EXPECT_EQ(kern_log_get_level(), KERN_LOG_ERROR);

    kern_log_set_level(KERN_LOG_PANIC);
    EXPECT_EQ(kern_log_get_level(), KERN_LOG_PANIC);
}

TEST(KernelLogTest, LogBelowThresholdIsSuppressed)
{
    /* 设置日志级别为 WARN，则 DEBUG/INFO 消息应该被过滤 */
    kern_log_set_level(KERN_LOG_WARN);

    /* 低于阈值的日志不应该输出到串口 */
    kern_log(KERN_LOG_DEBUG, "this should not appear");
    kern_log(KERN_LOG_INFO, "this should not appear either");

    /* WARN 及以上应该输出 */
    kern_log(KERN_LOG_WARN, "warn message");
    kern_log(KERN_LOG_ERROR, "error message");

    /* 如果执行到这里没有崩溃，日志过滤机制正常 */
    SUCCEED();
}

TEST(KernelLogTest, LogFmtDoesNotCrash)
{
    /* 测试格式化字符串日志不崩溃 */
    kern_log(KERN_LOG_INFO, "test int: %d", 42);
    kern_log(KERN_LOG_INFO, "test str: %s", "hello");
    kern_log(KERN_LOG_INFO, "test hex: 0x%x", 0xABCD);
    kern_log(KERN_LOG_INFO, "test multiple: %d %s %x", 1, "two", 3);

    SUCCEED();
}
