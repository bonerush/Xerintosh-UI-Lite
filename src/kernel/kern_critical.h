#ifndef KERN_CRITICAL_H
#define KERN_CRITICAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t kern_enter_critical(void);
void     kern_exit_critical(uint32_t state);

bool kern_interrupts_enabled(void);
void kern_disable_interrupts(void);
void kern_enable_interrupts(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_CRITICAL_H */
