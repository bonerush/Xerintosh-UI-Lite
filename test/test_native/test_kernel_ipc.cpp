#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_ipc.h"
#include "kernel/kern_init.h"
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
