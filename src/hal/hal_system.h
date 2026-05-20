#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 生命周期 ─── */

extern void hal_system_init(void);

/* ─── 操作函数 ─── */

extern uint32_t hal_get_ticks(void);
extern void hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYSTEM_H */
