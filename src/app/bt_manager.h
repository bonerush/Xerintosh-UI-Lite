#ifndef BT_MANAGER_H
#define BT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 类型定义 ─── */

typedef enum {
    BT_MGR_IDLE,
    BT_MGR_WARMUP,
    BT_MGR_SCANNING,
    BT_MGR_SCAN_DONE,
    BT_MGR_PAIRING,
    BT_MGR_PAIRED,
    BT_MGR_PAIR_FAILED
} bt_mgr_state_t;

/* ─── 生命周期 ─── */

void bt_mgr_init(void);

/* ─── 操作函数 ─── */

void bt_mgr_enable(void);
void bt_mgr_disable(void);
bool bt_mgr_is_enabled(void);
bt_mgr_state_t bt_mgr_get_state(void);
bool bt_mgr_is_waiting_input(void);
void bt_mgr_update(void);
void bt_mgr_on_switch_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_MANAGER_H */
