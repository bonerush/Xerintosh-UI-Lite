#ifdef NATIVE_TEST

#include "app/wifi_manager.h"

void wifi_mgr_init(void) {}
void wifi_mgr_enable(void) {}
void wifi_mgr_disable(void) {}
bool wifi_mgr_is_enabled(void) { return false; }
wifi_mgr_state_t wifi_mgr_get_state(void) { return WIFI_MGR_IDLE; }
bool wifi_mgr_is_waiting_input(void) { return false; }
void wifi_mgr_update(void) {}
void wifi_mgr_on_switch_toggle(void) {}

#else

#include <WiFi.h>
#include <Arduino.h>
#include <string.h>
#include <esp_log.h>

#include "app/wifi_manager.h"

extern "C" {
#include "app/storage.h"
#include "app/serial_input.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
}

/* ─── External globals ─── */

extern bool wifi_on;   // defined in main.cpp

/* ─── Module state ─── */

static wifi_mgr_state_t g_state           = WIFI_MGR_IDLE;
static bool             g_wifi_enabled    = false;

static xerintosh_list_item_t *g_settings_list      = NULL;  // "Settings" list_item
static xerintosh_list_item_t *g_networks_list      = NULL;  // "Networks" list_item (child of Settings)
static xerintosh_list_item_t *g_saved_container    = NULL;  // "Saved" list_item (child of Networks)
static xerintosh_list_item_t *g_available_container = NULL; // "Available" list_item (child of Networks)
static xerintosh_list_item_t *g_scan_button        = NULL;

static bool  g_connecting = false;
static char  g_connecting_ssid[STORAGE_SSID_MAX_LEN] = {0};
static char  g_connecting_pass[STORAGE_PASS_MAX_LEN] = {0};

static unsigned long g_wifi_scan_start_time = 0;
static unsigned long g_warmup_start_time   = 0;
static unsigned long g_connect_start_time  = 0;
static int g_scan_retry_count = 0;

#define WIFI_WARMUP_DELAY_MS    3000
#define WIFI_SCAN_TIMEOUT_MS    30000
#define WIFI_CONNECT_TIMEOUT_MS 15000

/* ─── Forward declarations (callbacks) ─── */

static void on_network_button_pressed(void);
static void on_saved_connect_pressed(void);
static void on_saved_delete_pressed(void);
static void on_scan_pressed(void);
static void rebuild_network_list(int scan_count);
static void suppress_wifi_logs(void);
static void restore_wifi_logs(void);

/* ─── Public getters ─── */

bool wifi_mgr_is_enabled(void)
{
    return g_wifi_enabled;
}

wifi_mgr_state_t wifi_mgr_get_state(void)
{
    return g_state;
}

bool wifi_mgr_is_waiting_input(void)
{
    return g_state == WIFI_MGR_CONNECTING;
}

/* ─── Init ─── */

void wifi_mgr_init(void)
{
    /* Settings is the first child of root */
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root && root->child_num > 0) {
        g_settings_list = root->child_list_item[0];  // "Settings"
    }

    /* NOTE: Auto-connect removed. WiFi is off by default (wifi_on = false).
       Starting WiFi.begin() here causes scan failures later because
       WiFi.disconnect() leaves the driver in an unstable state.
       Connection is handled when user selects a network instead. */
}

/* ─── Enable / Disable ─── */

void wifi_mgr_enable(void)
{
    g_wifi_enabled = true;
    WiFi.mode(WIFI_STA);
    g_warmup_start_time = millis();
    g_state = WIFI_MGR_WARMUP;
    rebuild_network_list(0);
}

/* ─── Log suppression helpers ─── */

static void suppress_wifi_logs(void)
{
    esp_log_level_set("wifi", ESP_LOG_ERROR);
}

static void restore_wifi_logs(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
}

void wifi_mgr_disable(void)
{
    if (g_connecting) {
        restore_wifi_logs();
    }
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    g_wifi_enabled = false;

    if (g_networks_list) {
        /* If selector is currently inside Networks subtree, exit back to Settings */
        xerintosh_list_item_t *check = xerintosh_selector.selected_item;
        while (check && check != g_networks_list) {
            check = check->parent;
        }
        if (check == g_networks_list) {
            xerintosh_selector.selected_item  = g_settings_list->child_list_item[0];
            xerintosh_selector.selected_index = 0;
        }

        xerintosh_clear_children_of_list(g_networks_list);
        xerintosh_remove_item_from_list(g_settings_list, g_networks_list);
        g_networks_list = NULL;
    }

    g_saved_container     = NULL;
    g_available_container = NULL;
    g_scan_button         = NULL;
    g_connecting          = false;
    g_state               = WIFI_MGR_IDLE;
}

/* ─── Switch toggle (called as exit_function of WiFi switch) ─── */

void wifi_mgr_on_switch_toggle(void)
{
    if (wifi_on) {
        wifi_mgr_enable();
    } else {
        wifi_mgr_disable();
    }
}

/* ─── Network list rebuild ─── */

static void rebuild_network_list(int scan_count)
{
    /* Create the Networks list on first call */
    if (!g_networks_list) {
        g_networks_list = xerintosh_new_list_item("网络", list_icon);
        if (g_settings_list && g_networks_list) {
            xerintosh_push_item_to_list(g_settings_list, g_networks_list);
        }
    }

    if (!g_networks_list) {
        return;
    }

    /* Safety: if selector is inside Networks subtree, move it to Networks itself
       so clearing children doesn't create a dangling pointer. */
    xerintosh_list_item_t *check = xerintosh_selector.selected_item;
    while (check && check != g_networks_list) {
        check = check->parent;
    }
    if (check == g_networks_list) {
        uint8_t idx = 0;
        if (g_settings_list) {
            for (uint8_t i = 0; i < g_settings_list->child_num; i++) {
                if (g_settings_list->child_list_item[i] == g_networks_list) {
                    idx = i;
                    break;
                }
            }
        }
        xerintosh_selector.selected_item = g_networks_list;
        xerintosh_selector.selected_index = idx;
    }

    xerintosh_clear_children_of_list(g_networks_list);

    /* ─── 1. 已保存网络容器 ─── */
    g_saved_container = xerintosh_new_list_item("已保存", list_icon);
    xerintosh_push_item_to_list(g_networks_list, g_saved_container);

    int saved_count = storage_wifi_get_count();
    for (int i = 0; i < saved_count; i++) {
        char ssid[STORAGE_SSID_MAX_LEN];
        char pass[STORAGE_PASS_MAX_LEN];
        if (!storage_wifi_get(i, ssid, pass)) {
            continue;
        }

        xerintosh_list_item_t *net_item = xerintosh_new_list_item(ssid, list_icon);
        xerintosh_list_item_t *connect_btn = xerintosh_new_button_item("连接", on_saved_connect_pressed, default_icon);
        xerintosh_list_item_t *del_btn = xerintosh_new_button_item("删除", on_saved_delete_pressed, default_icon);
        xerintosh_push_item_to_list(net_item, connect_btn);
        xerintosh_push_item_to_list(net_item, del_btn);
        xerintosh_push_item_to_list(g_saved_container, net_item);
    }

    /* ─── 2. 可用网络容器（仅未保存的扫描结果） ─── */
    g_available_container = xerintosh_new_list_item("可用网络", list_icon);
    xerintosh_push_item_to_list(g_networks_list, g_available_container);

    int show_count = scan_count;
    if (show_count > 9) {
        show_count = 9;
    }
    for (int i = 0; i < show_count; i++) {
        String ssid = WiFi.SSID(i);
        if (storage_wifi_find(ssid.c_str()) >= 0) {
            continue; /* skip already-saved networks */
        }
        xerintosh_list_item_t *item = xerintosh_new_button_item(ssid.c_str(), on_network_button_pressed, default_icon);
        xerintosh_push_item_to_list(g_available_container, item);
    }

    /* ─── 3. 扫描按钮 ─── */
    g_scan_button = xerintosh_new_button_item("扫描", on_scan_pressed, default_icon);
    xerintosh_push_item_to_list(g_networks_list, g_scan_button);

    /* After rebuild, if selector was on Networks, move it to the first child
       so the user sees the network list content immediately. */
    if (xerintosh_selector.selected_item == g_networks_list && g_networks_list->child_num > 0) {
        xerintosh_selector.selected_item = g_networks_list->child_list_item[0];
        xerintosh_selector.selected_index = 0;
    }

    /* Free the scan result buffer so the next scan can start cleanly. */
    WiFi.scanDelete();
}

/* ─── Callbacks ─── */

static void on_network_button_pressed(void)
{
    const char *content = xerintosh_selector.selected_item->content;
    if (!content) {
        return;
    }

    xerintosh_push_pop_up("请在串口输入密码", 8000);
    serial_request_wifi_password(content);
    g_state = WIFI_MGR_CONNECTING;
}

static void on_saved_connect_pressed(void)
{
    const char *content = xerintosh_selector.selected_item->parent->content;
    if (!content) {
        return;
    }

    char ssid[STORAGE_SSID_MAX_LEN];
    char pass[STORAGE_PASS_MAX_LEN];
    int idx = storage_wifi_find(content);
    if (idx < 0) {
        return;
    }
    if (!storage_wifi_get(idx, ssid, pass)) {
        return;
    }

    suppress_wifi_logs();
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
    strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
    g_connecting = true;
    g_connect_start_time = millis();
    xerintosh_push_pop_up("连接中...", 3000);

    xerintosh_selector_exit_current_item();
}

static void on_saved_delete_pressed(void)
{
    const char *content = xerintosh_selector.selected_item->parent->content;
    if (!content) {
        return;
    }

    int idx = storage_wifi_find(content);
    if (idx >= 0) {
        storage_wifi_remove(idx);
    }

    xerintosh_push_pop_up("已删除", 1500);
    xerintosh_selector_exit_current_item();
    rebuild_network_list(WiFi.scanComplete());
}

static void on_scan_pressed(void)
{
    if (g_connecting) {
        WiFi.disconnect();
        restore_wifi_logs();
        g_connecting = false;
    }
    WiFi.scanDelete();          /* free any stale results first */
    int16_t scan_ret = WiFi.scanNetworks(true);    /* async */
    if (scan_ret == -2) {
        xerintosh_push_pop_up("扫描失败", 2000);
        g_state = WIFI_MGR_IDLE;
    } else if (scan_ret >= 0) {
        /* Scan completed synchronously */
        xerintosh_hide_pop_up();
        rebuild_network_list(scan_ret);
        g_state = WIFI_MGR_SCAN_DONE;
        g_scan_retry_count = 0;
    } else {
        g_state = WIFI_MGR_SCANNING;
        g_wifi_scan_start_time = millis();
        xerintosh_push_pop_up("扫描中...", WIFI_SCAN_TIMEOUT_MS);
    }
}

/* ─── Per-frame update (non-blocking state machine) ─── */

#define WIFI_SCAN_MAX_RETRIES 3

void wifi_mgr_update(void)
{
    if (!g_wifi_enabled && g_state == WIFI_MGR_IDLE) {
        return;
    }

    switch (g_state) {

    case WIFI_MGR_WARMUP: {
        if (millis() - g_warmup_start_time >= WIFI_WARMUP_DELAY_MS) {
            on_scan_pressed();
        }
        break;
    }

    case WIFI_MGR_SCANNING: {
        /* 扫描超时检查 */
        if (millis() - g_wifi_scan_start_time >= WIFI_SCAN_TIMEOUT_MS) {
            WiFi.scanDelete();
            xerintosh_hide_pop_up();
            rebuild_network_list(0);
            g_state = WIFI_MGR_SCAN_DONE;
            g_scan_retry_count = 0;
            break;
        }

        int16_t result = WiFi.scanComplete();
        if (result >= 0) {
            xerintosh_hide_pop_up();
            rebuild_network_list(result);
            g_state = WIFI_MGR_SCAN_DONE;
            g_scan_retry_count = 0;
        } else if (result == -2) {
            /* 扫描失败 - retry if under limit */
            if (g_scan_retry_count < WIFI_SCAN_MAX_RETRIES) {
                g_scan_retry_count++;
                WiFi.scanDelete();
                WiFi.scanNetworks(true);
                g_wifi_scan_start_time = millis();
            } else {
                xerintosh_hide_pop_up();
                xerintosh_push_pop_up("扫描失败", 2000);
                g_state = WIFI_MGR_IDLE;
                g_scan_retry_count = 0;
            }
        }
        /* result == -1 => still scanning, wait */
        break;
    }

    case WIFI_MGR_CONNECTING: {
        /* Poll serial for password input */
        serial_state_t ss = serial_poll();
        if (ss == SERIAL_STATE_PASSWORD_RECEIVED) {
            const char *input  = serial_get_input();
            const char *target = serial_get_target_name();
            if (input && target) {
                suppress_wifi_logs();
                WiFi.disconnect();
                WiFi.begin(target, input);
                strncpy(g_connecting_ssid, target, STORAGE_SSID_MAX_LEN);
                strncpy(g_connecting_pass, input,  STORAGE_PASS_MAX_LEN);
                g_connecting = true;
                g_connect_start_time = millis();
                Serial.println("CONNECTING...");
                xerintosh_push_pop_up("连接中...", 3000);
            }
        } else if (ss == SERIAL_STATE_CANCELLED) {
            restore_wifi_logs();
            g_connecting = false;
            g_state = WIFI_MGR_SCAN_DONE;
            xerintosh_push_pop_up("已取消", 1500);
        }

        /* Check WiFi connection status */
        if (g_connecting) {
            /* 连接超时检查 */
            if (millis() - g_connect_start_time >= WIFI_CONNECT_TIMEOUT_MS) {
                WiFi.disconnect();
                restore_wifi_logs();
                g_connecting = false;
                Serial.println("TIMEOUT");
                xerintosh_push_pop_up("连接超时", 2000);
                g_state = WIFI_MGR_CONNECT_FAILED;
                break;
            }

            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                restore_wifi_logs();
                g_connecting = false;
                storage_wifi_add(g_connecting_ssid, g_connecting_pass);
                Serial.println("OK");
                xerintosh_push_pop_up("已连接", 2000);
                g_state = WIFI_MGR_CONNECTED;
                rebuild_network_list(WiFi.scanComplete());
            } else if (status == WL_CONNECT_FAILED ||
                       status == WL_NO_SSID_AVAIL) {
                restore_wifi_logs();
                g_connecting = false;
                Serial.println("FAIL");
                xerintosh_push_pop_up("连接失败", 2000);
                g_state = WIFI_MGR_CONNECT_FAILED;
            }
            /* WL_IDLE_STATUS / WL_DISCONNECTED => still trying */
        }
        break;
    }

    case WIFI_MGR_CONNECTED:
    case WIFI_MGR_CONNECT_FAILED:
        /* Stay until user takes action (rescan, select another network) */
        break;

    default:
        break;
    }
}

#endif /* NATIVE_TEST */
