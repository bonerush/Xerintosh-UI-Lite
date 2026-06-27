#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_ipc.h"
#include "kernel/kern_init.h"
#include "kernel/kern_task.h"
#include "kernel/kern_task_ctrl.h"
#include "kernel/kern_sched.h"
}

TEST(KernelIpcTest, BinSemGiveTake)
{
    kern_init();
    kern_bin_sem_t sem;
    kern_bin_sem_init(&sem, 0);

    EXPECT_EQ(kern_bin_sem_take(&sem, 0), KERN_ETIMEOUT);
    EXPECT_EQ(kern_bin_sem_give(&sem), KERN_OK);
    EXPECT_EQ(kern_bin_sem_take(&sem, 0), KERN_OK);
}

TEST(KernelIpcTest, CountingSemGiveTake)
{
    kern_init();
    kern_sem_t sem;
    kern_sem_init(&sem, 2, 3);

    EXPECT_EQ(kern_sem_take(&sem, 0), KERN_OK);
    EXPECT_EQ(kern_sem_take(&sem, 0), KERN_OK);
    EXPECT_EQ(kern_sem_get_count(&sem), 0);
    EXPECT_EQ(kern_sem_take(&sem, 0), KERN_ETIMEOUT);
}

TEST(KernelIpcTest, PiMutexRecursiveLock)
{
    kern_init();
    kern_sched_init();

    kern_pi_mutex_t m;
    kern_pi_mutex_init(&m);

    EXPECT_EQ(kern_pi_mutex_lock(&m, 0), KERN_OK);
    EXPECT_EQ(kern_pi_mutex_lock(&m, 0), KERN_OK);
    EXPECT_EQ(kern_pi_mutex_unlock(&m), KERN_OK);
    EXPECT_EQ(kern_pi_mutex_unlock(&m), KERN_OK);
    EXPECT_EQ(kern_pi_mutex_unlock(&m), KERN_EPERM);
}

TEST(KernelIpcTest, QueueSendRecv)
{
    kern_init();
    uint8_t buf[64];
    kern_queue_t q;
    kern_queue_init(&q, buf, sizeof(uint32_t), 4);

    uint32_t msg = 0xDEADBEEF;
    EXPECT_EQ(kern_queue_send(&q, &msg, 0), KERN_OK);

    uint32_t out = 0;
    EXPECT_EQ(kern_queue_recv(&q, &out, 0), KERN_OK);
    EXPECT_EQ(out, 0xDEADBEEFu);
}

TEST(KernelIpcTest, EventWaitAny)
{
    kern_init();
    kern_event_t ev;
    kern_event_init(&ev);

    kern_event_set(&ev, 0x02);
    EXPECT_EQ(kern_event_wait(&ev, 0x03, KERN_EVENT_WAIT_ANY, 0), KERN_OK);
}

/* ═══ PI 互斥锁优先级记录缺陷回归测试 ═══
 *
 * 旧实现把 m->orig_priority 记录为获取锁时的当前优先级；
 * 如果持有者随后通过 kern_task_priority_set 修改了 base_priority，
 * 解锁时仍会恢复到过时的 orig_priority，而不是新的 base_priority。
 * 新实现直接使用 owner->base_priority 作为恢复目标。
 */

static kern_pi_mutex_t    g_pi_base_mutex;
static volatile bool      g_pi_high_started = false;
static volatile bool      g_pi_high_done    = false;
static volatile uint8_t   g_pi_low_prio_after_boost = 0;
static volatile uint8_t   g_pi_low_prio_after_unlock = 0;

static void pi_base_low_task(void *arg)
{
    (void)arg;

    /* 低优先级任务作为 mutex 持有者 */
    kern_task_priority_set(NULL, 1);
    EXPECT_EQ(kern_pi_mutex_lock(&g_pi_base_mutex, 0), KERN_OK);

    /* 模拟运行时动态调整基础优先级 */
    kern_task_priority_set(NULL, 5);

    /* 通知高优先级任务可以来竞争锁 */
    g_pi_high_started = true;

    /* 等待高优先级任务阻塞并把本任务提升到其优先级 */
    for (int i = 0; i < 1000 && kern_task_priority_get(NULL) != 10; i++) {
        kern_yield();
    }

    g_pi_low_prio_after_boost = kern_task_priority_get(NULL);

    /* 解锁时应恢复到更新后的 base_priority，而非旧 orig_priority */
    EXPECT_EQ(kern_pi_mutex_unlock(&g_pi_base_mutex), KERN_OK);
    g_pi_low_prio_after_unlock = kern_task_priority_get(NULL);

    /* 等待高优先级任务完成 */
    for (int i = 0; i < 1000 && !g_pi_high_done; i++) {
        kern_yield();
    }

    kern_exit();
}

static void pi_base_high_task(void *arg)
{
    (void)arg;

    kern_task_priority_set(NULL, 10);
    while (!g_pi_high_started) {
        kern_yield();
    }

    /* 阻塞等待 mutex，会触发优先级继承 */
    kern_err_t rc = kern_pi_mutex_lock(&g_pi_base_mutex, 1000);
    EXPECT_EQ(rc, KERN_OK);
    EXPECT_EQ(kern_pi_mutex_unlock(&g_pi_base_mutex), KERN_OK);
    g_pi_high_done = true;
    kern_exit();
}

TEST(KernelIpcTest, PiMutexRestoresUpdatedBasePriority)
{
    kern_init();
    kern_sched_init();

    kern_pi_mutex_init(&g_pi_base_mutex);
    g_pi_high_started = false;
    g_pi_high_done    = false;
    g_pi_low_prio_after_boost  = 0;
    g_pi_low_prio_after_unlock = 0;

    kern_pid_t low_pid = kern_spawn("pi_low", pi_base_low_task, NULL, 0);
    kern_pid_t high_pid = kern_spawn("pi_high", pi_base_high_task, NULL, 0);
    ASSERT_GE(low_pid, 0);
    ASSERT_GE(high_pid, 0);

    /* 运行足够 tick 让两个任务完成交互 */
    for (int i = 0; i < 5000 && !g_pi_high_done; i++) {
        kern_sched_tick();
    }

    EXPECT_EQ(g_pi_low_prio_after_boost, 10u)
        << "low-priority owner should be boosted to high waiter's priority";
    EXPECT_EQ(g_pi_low_prio_after_unlock, 5u)
        << "priority should restore to updated base (5), not stale orig_priority (1)";

    /* 等待两个 spawned 任务都退出并被回收，避免泄漏到后续测试 */
    for (int i = 0; i < 2000 && (kern_task_get(low_pid) != NULL || kern_task_get(high_pid) != NULL); i++) {
        kern_sched_tick();
    }
}
