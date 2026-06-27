#include <gtest/gtest.h>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_ipc.h"
#include "kernel/kern_isr.h"
#include "kernel/kern_critical.h"
#include "kernel/kern_init.h"
}

TEST(KernelIsrTest, BinSemGiveFromIsrSetsCountAndWokenFlag)
{
    kern_init();
    kern_bin_sem_t sem;
    kern_bin_sem_init(&sem, 0);

    bool woken = false;
    EXPECT_EQ(kern_bin_sem_give_from_isr(&sem, &woken), KERN_OK);
    EXPECT_EQ(sem.count, 1);
    EXPECT_FALSE(woken);
}

TEST(KernelIsrTest, SemGiveFromIsrRespectsMax)
{
    kern_init();
    kern_sem_t sem;
    kern_sem_init(&sem, 0, 2);

    bool woken = false;
    kern_sem_give_from_isr(&sem, &woken);
    kern_sem_give_from_isr(&sem, &woken);
    kern_sem_give_from_isr(&sem, &woken);

    EXPECT_EQ(kern_sem_get_count(&sem), 2);
}

TEST(KernelIsrTest, EventSetFromIsrSetsBits)
{
    kern_init();
    kern_event_t ev;
    kern_event_init(&ev);

    bool woken = false;
    kern_event_set_from_isr(&ev, 0x05, &woken);
    EXPECT_EQ(kern_event_get(&ev), 0x05u);
}

TEST(KernelIsrTest, CriticalSectionNests)
{
    uint32_t s1 = kern_enter_critical();
    uint32_t s2 = kern_enter_critical();
    EXPECT_EQ(s1, s2);
    kern_exit_critical(s2);
    kern_exit_critical(s1);
}
