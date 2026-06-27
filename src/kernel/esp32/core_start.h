#ifndef CORE_START_H
#define CORE_START_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void xeros_core_start(uint8_t cpu_id, void (*entry)(void *arg));

#ifdef __cplusplus
}
#endif

#endif /* CORE_START_H */
