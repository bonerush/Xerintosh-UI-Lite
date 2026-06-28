#include "kern_critical.h"

#if defined(XEROS_NATIVE_SCHED) && !defined(NATIVE_TEST)

#include <xtensa/config/core.h>

uint32_t kern_enter_critical(void)
{
    uint32_t ps;
    __asm__ volatile ("rsr %0, ps" : "=r"(ps));
    uint32_t old = ps & XCHAL_PS_INTLEVEL_MASK;
    ps = (ps & ~XCHAL_PS_INTLEVEL_MASK) | XCHAL_EXCM_LEVEL;
    __asm__ volatile ("wsr %0, ps" :: "r"(ps));
    return old;
}

void kern_exit_critical(uint32_t state)
{
    uint32_t ps;
    __asm__ volatile ("rsr %0, ps" : "=r"(ps));
    ps = (ps & ~XCHAL_PS_INTLEVEL_MASK) | (state & XCHAL_PS_INTLEVEL_MASK);
    __asm__ volatile ("wsr %0, ps" :: "r"(ps));
}

bool kern_interrupts_enabled(void)
{
    uint32_t ps;
    __asm__ volatile ("rsr %0, ps" : "=r"(ps));
    return (ps & XCHAL_PS_INTLEVEL_MASK) == 0;
}

void kern_disable_interrupts(void)
{
    kern_enter_critical();
}

void kern_enable_interrupts(void)
{
    kern_exit_critical(0);
}

#else /* NATIVE_TEST */

uint32_t kern_enter_critical(void) { return 0; }
void     kern_exit_critical(uint32_t state) { (void)state; }
bool     kern_interrupts_enabled(void) { return true; }
void     kern_disable_interrupts(void) {}
void     kern_enable_interrupts(void) {}

#endif
