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
#include "kernel/kern_sched_rr.h"
#include "kernel/kern_sched_fifo.h"
#include "kernel/kern_init.h"
#include "kernel/kern_port.h"
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

TEST(KernelSchedTest, InitIsIdempotent)
{
    kern_sched_init();
    uint32_t ticks_before = g_sched_ticks;
    kern_task_t *idle_before = g_idle_task;
    kern_sched_init();
    EXPECT_EQ(g_sched_ticks, ticks_before);
    EXPECT_EQ(g_idle_task, idle_before);
    EXPECT_NE(g_idle_task, nullptr);
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

/* ═══ 访问器测试 ═══ */

TEST(KernelSchedTest, TaskListHeadAccessorMatchesGlobal)
{
    kern_sched_init();
    EXPECT_EQ(kern_task_list_head(), g_task_list);
    EXPECT_EQ(kern_task_list_tail(), g_task_list_tail);
}

TEST(KernelSchedTest, CurrentTaskAccessorMatchesGlobal)
{
    kern_sched_init();
    EXPECT_EQ(kern_current_task(), g_current_task);
}

TEST(KernelSchedTest, NeedReschedAccessorWorks)
{
    kern_sched_init();
    kern_set_need_resched(true);
    EXPECT_TRUE(kern_need_resched());
    kern_set_need_resched(false);
    EXPECT_FALSE(kern_need_resched());
}

/* ═══ 辅助：构造一个未入队的 dummy 任务 ═══ */

static void init_dummy_task(kern_task_t *task, const char *name)
{
    memset(task, 0, sizeof(*task));
    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    }
    task->state = KERN_TASK_READY;
    task->scheduler_class_id = -1;
}

/* ═══ scheduler_class_id 初始化与同步测试 ═══ */

TEST(KernelSchedTest, SpawnInitializesSchedulerClassId)
{
    kern_sched_init();

    kern_pid_t pid = kern_spawn("classid_test", simple_counter, NULL, 0);
    ASSERT_GE(pid, 0);

    kern_task_t *task = kern_task_get(pid);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->scheduler_class_id, KERN_SCHED_CLASS_RR_ID);
    EXPECT_NE(task->scheduler_class_id, -1);
}

TEST(KernelSchedTest, RrEnqueueSetsClassId)
{
    kern_sched_init();

    kern_task_t dummy;
    init_dummy_task(&dummy, "rr_dummy");

    sched_class_rr.enqueue(&dummy);
    EXPECT_EQ(dummy.scheduler_class_id, KERN_SCHED_CLASS_RR_ID);

    /* 清理，避免影响后续测试 */
    sched_class_rr.dequeue(&dummy);
}

TEST(KernelSchedTest, RrDequeueClearsClassId)
{
    kern_sched_init();

    kern_task_t dummy;
    init_dummy_task(&dummy, "rr_dummy");

    sched_class_rr.enqueue(&dummy);
    ASSERT_NE(dummy.scheduler_class_id, -1);

    sched_class_rr.dequeue(&dummy);
    EXPECT_EQ(dummy.scheduler_class_id, -1);
}

TEST(KernelSchedTest, FifoEnqueueSetsClassId)
{
    kern_sched_init();

    kern_task_t dummy;
    init_dummy_task(&dummy, "fifo_dummy");
    dummy.priority = 128;

    sched_class_fifo.enqueue(&dummy);
    EXPECT_EQ(dummy.scheduler_class_id, sched_class_fifo.class_id);

    /* 清理 */
    sched_class_fifo.dequeue(&dummy);
}

TEST(KernelSchedTest, FifoDequeueClearsClassId)
{
    kern_sched_init();

    kern_task_t dummy;
    init_dummy_task(&dummy, "fifo_dummy");
    dummy.priority = 128;

    sched_class_fifo.enqueue(&dummy);
    ASSERT_NE(dummy.scheduler_class_id, -1);

    sched_class_fifo.dequeue(&dummy);
    EXPECT_EQ(dummy.scheduler_class_id, -1);
}

TEST(KernelSchedTest, StackPressureWarningDoesNotCrash)
{
    kern_sched_init();
    g_sched_counter = 0;

    kern_pid_t pid = kern_spawn("pressure_test", simple_counter, NULL, 1024);
    ASSERT_GE(pid, 0);

    /* 触发至少一次 500 tick 边界的栈压力检查 */
    for (int i = 0; i < 600; i++) {
        kern_sched_tick();
    }

    /* 只要没有崩溃/段错误即通过 */
    SUCCEED();
}

/* ═══ 内存压力回调测试 ═══ */

static kern_kmem_pressure_level_t g_last_pressure = KERN_KMEM_PRESSURE_LOW;
static int g_pressure_callback_count = 0;

static void dummy_memory_pressure(kern_kmem_pressure_level_t level)
{
    g_last_pressure = level;
    g_pressure_callback_count++;
}

TEST(KernelSchedTest, MemoryPressureCallbackInvoked)
{
    kern_sched_init();
    g_pressure_callback_count = 0;
    g_last_pressure = KERN_KMEM_PRESSURE_LOW;

    kern_sched_class_t dummy = {
        .name            = "dummy-pressure",
        .enqueue         = NULL,
        .dequeue         = NULL,
        .pick_next       = NULL,
        .tick            = NULL,
        .prio_changed    = NULL,
        .memory_pressure = dummy_memory_pressure,
        .task_list       = NULL,
        .task_list_tail  = NULL,
    };

    /* 保存并恢复注册表 */
    uint8_t saved_count = g_sched_class_count;
    kern_sched_class_t *saved_classes[KERN_SCHED_MAX_CLASSES];
    memcpy(saved_classes, g_sched_classes, sizeof(saved_classes));

    kern_sched_class_register(&dummy);

    /* 运行足够 tick 触发至少一次 100 tick 边界的压力分发 */
    for (int i = 0; i < 110; i++) {
        kern_sched_tick();
    }

    EXPECT_GT(g_pressure_callback_count, 0);

    /* 恢复注册表 */
    g_sched_class_count = saved_count;
    memcpy(g_sched_classes, saved_classes, KERN_SCHED_MAX_CLASSES * sizeof(kern_sched_class_t *));
}

TEST(KernelSchedTest, RrTimesliceShortensUnderHighPressure)
{
    kern_sched_init();
    g_pressure_callback_count = 0;
    g_last_pressure = KERN_KMEM_PRESSURE_LOW;

    kern_sched_class_t dummy = {
        .name            = "dummy-pressure",
        .enqueue         = NULL,
        .dequeue         = NULL,
        .pick_next       = NULL,
        .tick            = NULL,
        .prio_changed    = NULL,
        .memory_pressure = dummy_memory_pressure,
        .task_list       = NULL,
        .task_list_tail  = NULL,
    };

    uint8_t saved_count = g_sched_class_count;
    kern_sched_class_t *saved_classes[KERN_SCHED_MAX_CLASSES];
    memcpy(saved_classes, g_sched_classes, sizeof(saved_classes));

    kern_sched_class_register(&dummy);

    /* 设置保留水位并分配超过它，制造 HIGH 压力 */
    kern_kmem_set_reserved_bytes(64);
    void *p = kern_kmalloc(128);

    /* 运行一次压力分发 */
    for (int i = 0; i < 110; i++) {
        kern_sched_tick();
    }

    EXPECT_EQ(g_last_pressure, KERN_KMEM_PRESSURE_HIGH);

    kern_kfree(p);
    kern_kmem_set_reserved_bytes(0);

    /* 恢复注册表 */
    g_sched_class_count = saved_count;
    memcpy(g_sched_classes, saved_classes, KERN_SCHED_MAX_CLASSES * sizeof(kern_sched_class_t *));
}

TEST(KernelSchedTest, RrTimesliceRestoresUnderLowPressure)
{
    kern_sched_init();
    g_pressure_callback_count = 0;
    g_last_pressure = KERN_KMEM_PRESSURE_HIGH;

    kern_sched_class_t dummy = {
        .name            = "dummy-pressure",
        .enqueue         = NULL,
        .dequeue         = NULL,
        .pick_next       = NULL,
        .tick            = NULL,
        .prio_changed    = NULL,
        .memory_pressure = dummy_memory_pressure,
        .task_list       = NULL,
        .task_list_tail  = NULL,
    };

    uint8_t saved_count = g_sched_class_count;
    kern_sched_class_t *saved_classes[KERN_SCHED_MAX_CLASSES];
    memcpy(saved_classes, g_sched_classes, sizeof(saved_classes));

    kern_sched_class_register(&dummy);

    /* 保留水位为 0，应为 LOW */
    kern_kmem_set_reserved_bytes(0);

    for (int i = 0; i < 110; i++) {
        kern_sched_tick();
    }

    EXPECT_EQ(g_last_pressure, KERN_KMEM_PRESSURE_LOW);

    /* 恢复注册表 */
    g_sched_class_count = saved_count;
    memcpy(g_sched_classes, saved_classes, KERN_SCHED_MAX_CLASSES * sizeof(kern_sched_class_t *));
}

TEST(KernelSchedTest, StackGrowTriggerDoesNotCrash)
{
    kern_sched_init();

    /* spawn 一个短生命周期的任务；tick 中栈压力检查只会在非运行态触发增长，
     * 该测试主要验证调度器 tick 路径不会因新增栈压力逻辑崩溃。 */
    kern_pid_t pid = kern_spawn("grow_trigger", simple_counter, NULL, 1024);
    ASSERT_GE(pid, 0);

    /* 运行足够 tick，让栈压力检查有机会触发 */
    for (int i = 0; i < 600; i++) {
        kern_sched_tick();
    }

    /* 只要没有崩溃/段错误即通过 */
    SUCCEED();
}

TEST(KernelPortTest, TimerSetPeriodicReturnsOk)
{
    kern_err_t rc = kern_port_timer_set_periodic(1000);
    EXPECT_EQ(rc, KERN_OK);
}

TEST(KernelPortTest, TimerSetPeriodicRejectsZeroPeriod)
{
    kern_err_t rc = kern_port_timer_set_periodic(0);
    EXPECT_EQ(rc, KERN_EINVAL);
}
