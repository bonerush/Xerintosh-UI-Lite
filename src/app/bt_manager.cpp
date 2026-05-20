#ifdef NATIVE_TEST

#include "app/bt_manager.h"

void bt_mgr_init(void) {}
void bt_mgr_enable(void) {}
void bt_mgr_disable(void) {}
bool bt_mgr_is_enabled(void) { return false; }
bt_mgr_state_t bt_mgr_get_state(void) { return BT_MGR_IDLE; }
bool bt_mgr_is_waiting_input(void) { return false; }
void bt_mgr_update(void) {}
void bt_mgr_on_switch_toggle(void) {}

#else

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "app/bt_manager.h"

extern "C" {
#include "app/storage.h"
#include "app/serial_input.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
}

extern bool bt_on;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static bool g_bt_enabled = false;
static bt_mgr_state_t g_state = BT_MGR_IDLE;

// BLE scan results
struct BleDeviceResult {
    char name[STORAGE_BT_NAME_MAX_LEN];
    char address[STORAGE_BT_ADDR_MAX_LEN];
    int rssi;
};

static BleDeviceResult g_scan_results[20];
static int g_scan_result_count = 0;
static unsigned long g_scan_start_time = 0;
static unsigned long g_warmup_start_time = 0;
static const unsigned long SCAN_DURATION_MS = 10000;
#define BT_WARMUP_DELAY_MS 1500

// Menu items
static astra_list_item_t *g_settings_list = NULL;
static astra_list_item_t *g_devices_list = NULL;

// Forward declarations
static void rebuild_device_list(void);
static void on_device_button_pressed(void);
static void on_bt_reconnect_pressed(void);
static void on_bt_delete_pressed(void);
static void on_bt_scan_pressed(void);

// ---------------------------------------------------------------------------
// BLE scan callback (NimBLE API)
// ---------------------------------------------------------------------------
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (g_scan_result_count >= 20) return;

        BleDeviceResult &r = g_scan_results[g_scan_result_count];
        std::string addr = advertisedDevice->getAddress().toString();
        strlcpy(r.address, addr.c_str(), STORAGE_BT_ADDR_MAX_LEN);

        std::string name = advertisedDevice->getName();
        if (name.empty()) {
            name = addr;
        }
        strlcpy(r.name, name.c_str(), STORAGE_BT_NAME_MAX_LEN);

        r.rssi = advertisedDevice->getRSSI();
        g_scan_result_count++;
    }
};

// ---------------------------------------------------------------------------
// Menu callbacks
// ---------------------------------------------------------------------------
static void on_device_button_pressed(void) {
    astra_list_item_t *item = astra_selector.selected_item;
    if (!item || !item->content) return;
    astra_push_pop_up("请在串口输入配对码", 100);
    const char *addr = (const char*)item->user_data;
    if (addr && addr[0]) {
        serial_request_bt_pair_code_with_addr(item->content, addr);
    } else {
        serial_request_bt_pair_code(item->content);
    }
    g_state = BT_MGR_PAIRING;
}

static void on_bt_reconnect_pressed(void) {
    astra_list_item_t *parent = astra_selector.selected_item->parent;
    if (!parent || !parent->user_data) return;

    char addr[STORAGE_BT_ADDR_MAX_LEN];
    strlcpy(addr, (const char*)parent->user_data, sizeof(addr));

    astra_push_pop_up("搜索中...", 2000);
    astra_selector_exit_current_item();

    g_scan_result_count = 0;
    NimBLEDevice::getScan()->start(SCAN_DURATION_MS / 1000, false);
    g_scan_start_time = millis();
    g_state = BT_MGR_SCANNING;
}

static void on_bt_delete_pressed(void) {
    astra_list_item_t *parent = astra_selector.selected_item->parent;
    if (!parent || !parent->user_data) return;

    char addr[STORAGE_BT_ADDR_MAX_LEN];
    strlcpy(addr, (const char*)parent->user_data, sizeof(addr));

    int idx = storage_bt_find(addr);
    if (idx >= 0) storage_bt_remove(idx);

    astra_push_pop_up("已删除", 1500);
    astra_selector_exit_current_item();
    rebuild_device_list();
}

static void on_bt_scan_pressed(void) {
    g_scan_result_count = 0;
    NimBLEDevice::getScan()->start(SCAN_DURATION_MS / 1000, false);
    g_scan_start_time = millis();
    g_state = BT_MGR_SCANNING;
    astra_push_pop_up("扫描中...", SCAN_DURATION_MS);
}

// ---------------------------------------------------------------------------
// Device list builder
// ---------------------------------------------------------------------------
static void rebuild_device_list(void) {
    /* Create the Devices list on first call */
    if (!g_devices_list) {
        g_devices_list = astra_new_list_item("蓝牙设备", list_icon);
        if (g_settings_list && g_devices_list) {
            astra_push_item_to_list(g_settings_list, g_devices_list);
        }
    }

    if (!g_devices_list) return;

    /* Safety: if selector is inside Devices subtree, move it to Devices itself
       so clearing children doesn't create a dangling pointer. */
    astra_list_item_t *check = astra_selector.selected_item;
    while (check && check != g_devices_list) check = check->parent;
    if (check == g_devices_list) {
        uint8_t idx = 0;
        if (g_settings_list) {
            for (uint8_t i = 0; i < g_settings_list->child_num; i++) {
                if (g_settings_list->child_list_item[i] == g_devices_list) {
                    idx = i;
                    break;
                }
            }
        }
        astra_selector.selected_item = g_devices_list;
        astra_selector.selected_index = idx;
    }

    astra_clear_children_of_list(g_devices_list);

    for (int i = 0; i < g_scan_result_count && i < 9; i++) {
        int8_t saved_idx = storage_bt_find(g_scan_results[i].address);

        char display_name[STORAGE_BT_NAME_MAX_LEN + 2];
        if (saved_idx >= 0) {
            snprintf(display_name, sizeof(display_name), "%s*",
                     g_scan_results[i].name);
        } else {
            snprintf(display_name, sizeof(display_name), "%s",
                     g_scan_results[i].name);
        }

        astra_list_item_t *item;
        if (saved_idx >= 0) {
            item = astra_new_list_item(display_name, list_icon);
            astra_list_item_t *reconnect =
                astra_new_button_item("重新连接", on_bt_reconnect_pressed,
                                      default_icon);
            astra_list_item_t *del =
                astra_new_button_item("删除", on_bt_delete_pressed,
                                      default_icon);
            astra_push_item_to_list(item, reconnect);
            astra_push_item_to_list(item, del);
        } else {
            item = astra_new_button_item(display_name,
                                         on_device_button_pressed, default_icon);
        }

        item->user_data = strdup(g_scan_results[i].address);
        astra_push_item_to_list(g_devices_list, item);
    }

    astra_list_item_t *scan_btn =
        astra_new_button_item("扫描", on_bt_scan_pressed, default_icon);
    astra_push_item_to_list(g_devices_list, scan_btn);

    /* After rebuild, if selector was on Devices, move it to the first child
       so the user sees the device list content immediately. */
    if (astra_selector.selected_item == g_devices_list && g_devices_list->child_num > 0) {
        astra_selector.selected_item = g_devices_list->child_list_item[0];
        astra_selector.selected_index = 0;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void bt_mgr_init(void) {
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;
    g_devices_list = NULL;
    g_scan_result_count = 0;

    astra_list_item_t *root = astra_get_root_list();
    if (root && root->child_num > 0) {
        g_settings_list = root->child_list_item[0];  // "Settings"
    }
}

void bt_mgr_enable(void) {
    g_bt_enabled = true;
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
    scan->setActiveScan(true);

    g_scan_result_count = 0;
    g_warmup_start_time = millis();
    g_state = BT_MGR_WARMUP;
    rebuild_device_list();
}

void bt_mgr_disable(void) {
    NimBLEDevice::deinit(true);
    g_bt_enabled = false;

    if (g_devices_list) {
        astra_list_item_t *check = astra_selector.selected_item;
        while (check && check != g_devices_list) check = check->parent;
        if (check == g_devices_list) {
            if (g_settings_list) {
                astra_selector.selected_item = g_settings_list->child_list_item[0];
                astra_selector.selected_index = 0;
            }
        }

        astra_clear_children_of_list(g_devices_list);
        if (g_settings_list) {
            astra_remove_item_from_list(g_settings_list, g_devices_list);
        }
        g_devices_list = NULL;
    }

    g_state = BT_MGR_IDLE;
}

bool bt_mgr_is_enabled(void) { return g_bt_enabled; }

bt_mgr_state_t bt_mgr_get_state(void) { return g_state; }

bool bt_mgr_is_waiting_input(void) {
    return g_state == BT_MGR_PAIRING;
}

void bt_mgr_update(void) {
    if (!g_bt_enabled && g_state == BT_MGR_IDLE) return;

    switch (g_state) {
    case BT_MGR_WARMUP: {
        if (millis() - g_warmup_start_time >= BT_WARMUP_DELAY_MS) {
            on_bt_scan_pressed();
        }
        break;
    }
    case BT_MGR_SCANNING: {
        if (millis() - g_scan_start_time >= SCAN_DURATION_MS) {
            NimBLEDevice::getScan()->stop();
            astra_hide_pop_up();
            rebuild_device_list();
            g_state = BT_MGR_SCAN_DONE;
        }
        break;
    }
    case BT_MGR_PAIRING: {
        /* Keep pop-up visible while waiting for input */
        astra_push_pop_up("请在串口输入配对码", 100);
        serial_state_t ss = serial_poll();
        if (ss == SERIAL_STATE_PAIR_CODE_RECEIVED) {
            const char *code = serial_get_input();
            const char *name = serial_get_target_name();
            const char *addr = serial_get_target_addr();
            if (code && name) {
                storage_bt_add(addr ? addr : name, name);
                Serial.println("OK");
                astra_push_pop_up("已配对", 2000);
                g_state = BT_MGR_PAIRED;
                rebuild_device_list();
            }
        } else if (ss == SERIAL_STATE_CANCELLED) {
            g_state = BT_MGR_SCAN_DONE;
            astra_push_pop_up("已取消", 1500);
        }
        break;
    }
    default:
        break;
    }
}

void bt_mgr_on_switch_toggle(void) {
    if (bt_on) {
        bt_mgr_enable();
    } else {
        bt_mgr_disable();
    }
}

#endif /* NATIVE_TEST */
