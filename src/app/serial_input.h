#ifndef SERIAL_INPUT_H
#define SERIAL_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERIAL_STATE_IDLE,
    SERIAL_STATE_WAITING_PASSWORD,
    SERIAL_STATE_PASSWORD_RECEIVED,
    SERIAL_STATE_WAITING_PAIR_CODE,
    SERIAL_STATE_PAIR_CODE_RECEIVED,
    SERIAL_STATE_CANCELLED
} serial_state_t;

void           serial_request_wifi_password(const char *ssid);
void           serial_request_bt_pair_code(const char *device_name);
void           serial_request_bt_pair_code_with_addr(const char *device_name, const char *device_addr);
void           serial_cancel(void);
serial_state_t serial_poll(void);
const char*    serial_get_input(void);
const char*    serial_get_target_name(void);
const char*    serial_get_target_addr(void);

#ifdef __cplusplus
}
#endif
#endif // SERIAL_INPUT_H
