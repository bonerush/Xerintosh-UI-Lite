#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern void hal_system_init(void);
extern uint32_t hal_get_ticks(void);
extern void hal_delay_ms(uint32_t ms);
#ifdef __cplusplus
}
#endif
#endif
