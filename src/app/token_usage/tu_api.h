#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float total_balance;
    float granted_balance;
    float topped_up_balance;
    bool is_available;
} tu_deepseek_balance_t;

typedef struct {
    tu_deepseek_balance_t deepseek;
    bool deepseek_ok;
    uint32_t last_update;
} tu_data_t;

bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out);
void tu_data_init(tu_data_t *data);

#ifdef __cplusplus
}
#endif
