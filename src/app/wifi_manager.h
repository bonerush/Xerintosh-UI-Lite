#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MGR_IDLE,
    WIFI_MGR_WARMUP,
    WIFI_MGR_SCANNING,
    WIFI_MGR_SCAN_DONE,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,
    WIFI_MGR_CONNECT_FAILED
} wifi_mgr_state_t;

void wifi_mgr_init(void);
void wifi_mgr_enable(void);
void wifi_mgr_disable(void);
bool wifi_mgr_is_enabled(void);
wifi_mgr_state_t wifi_mgr_get_state(void);
bool wifi_mgr_is_waiting_input(void);
void wifi_mgr_update(void);
void wifi_mgr_on_switch_toggle(void);

#ifdef __cplusplus
}
#endif
#endif // WIFI_MANAGER_H
