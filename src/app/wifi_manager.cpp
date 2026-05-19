#include <WiFi.h>
#include <Arduino.h>
#include <string.h>

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

static astra_list_item_t *g_settings_list  = NULL;  // "Settings" list_item
static astra_list_item_t *g_networks_list  = NULL;  // "Networks" list_item (child of Settings)
static astra_list_item_t *g_scan_button    = NULL;

static bool  g_connecting = false;
static char  g_connecting_ssid[STORAGE_SSID_MAX_LEN] = {0};
static char  g_connecting_pass[STORAGE_PASS_MAX_LEN] = {0};

/* ─── Forward declarations (callbacks) ─── */

static void on_network_button_pressed(void);
static void on_reconnect_pressed(void);
static void on_delete_pressed(void);
static void on_scan_pressed(void);
static void rebuild_network_list(int scan_count);

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
    astra_list_item_t *root = astra_get_root_list();
    if (root && root->child_num > 0) {
        g_settings_list = root->child_list_item[0];  // "Settings"
    }

    /* Try auto-connect with saved credentials */
    if (storage_wifi_get_count() > 0) {
        char ssid[STORAGE_SSID_MAX_LEN];
        char pass[STORAGE_PASS_MAX_LEN];
        if (storage_wifi_get(0, ssid, pass)) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, pass);
            g_connecting = true;
            strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
            /* Non-blocking - connection result handled in wifi_mgr_update() */
        }
    }
}

/* ─── Enable / Disable ─── */

void wifi_mgr_enable(void)
{
    g_wifi_enabled = true;
    WiFi.mode(WIFI_STA);

    /* If a background auto-connection is still in progress, stop it
       so the scan can acquire the WiFi radio. */
    if (g_connecting || WiFi.status() == WL_IDLE_STATUS) {
        WiFi.disconnect();
        g_connecting = false;
    }

    /* Wait for WiFi driver to become idle before starting scan. */
    delay(300);

    /* Delete any stale scan results so the new scan can start cleanly. */
    WiFi.scanDelete();

    /* Create "Networks" list and append to Settings */
    g_networks_list = astra_new_list_item("网络", list_icon);
    if (g_settings_list && g_networks_list) {
        astra_push_item_to_list(g_settings_list, g_networks_list);
    }

    /* Start async scan */
    int16_t scan_ret = WiFi.scanNetworks(true);
    if (scan_ret == -2) {
        astra_push_pop_up("扫描失败", 2000);
        g_state = WIFI_MGR_IDLE;
    } else {
        g_state = WIFI_MGR_SCANNING;
        astra_push_pop_up("扫描中...", 100);
    }
}

void wifi_mgr_disable(void)
{
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    g_wifi_enabled = false;

    if (g_networks_list) {
        /* If selector is currently inside Networks subtree, exit back to Settings */
        astra_list_item_t *check = astra_selector.selected_item;
        while (check && check != g_networks_list) {
            check = check->parent;
        }
        if (check == g_networks_list) {
            astra_selector.selected_item  = g_settings_list->child_list_item[0];
            astra_selector.selected_index = 0;
        }

        astra_clear_children_of_list(g_networks_list);
        astra_remove_item_from_list(g_settings_list, g_networks_list);
        g_networks_list = NULL;
    }

    g_scan_button = NULL;
    g_connecting  = false;
    g_state       = WIFI_MGR_IDLE;
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
    if (!g_networks_list) {
        return;
    }

    /* Safety: if selector is inside Networks subtree, move it to Networks itself
       so clearing children doesn't create a dangling pointer. */
    astra_list_item_t *check = astra_selector.selected_item;
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
        astra_selector.selected_item = g_networks_list;
        astra_selector.selected_index = idx;
    }

    astra_clear_children_of_list(g_networks_list);

    /* Cap to 9 results (MAX_LIST_CHILD_NUM - 1 for Scan button) */
    int show_count = scan_count;
    if (show_count > 9) {
        show_count = 9;
    }

    for (int i = 0; i < show_count; i++) {
        String ssid = WiFi.SSID(i);
        int8_t saved_idx = storage_wifi_find(ssid.c_str());

        /* Display name: "SSID" for unconfigured, "SSID*" for saved */
        char display_name[STORAGE_SSID_MAX_LEN + 2];
        if (saved_idx >= 0) {
            snprintf(display_name, sizeof(display_name), "%s*", ssid.c_str());
        } else {
            snprintf(display_name, sizeof(display_name), "%s", ssid.c_str());
        }

        astra_list_item_t *item;
        if (saved_idx >= 0) {
            /* Saved network -> list_item with Reconnect / Delete children */
            item = astra_new_list_item(display_name, list_icon);
            astra_list_item_t *reconnect = astra_new_button_item("重新连接", on_reconnect_pressed, default_icon);
            astra_list_item_t *del        = astra_new_button_item("删除",    on_delete_pressed,    default_icon);
            astra_push_item_to_list(item, reconnect);
            astra_push_item_to_list(item, del);
        } else {
            /* Unknown network -> button_item triggers serial password input */
            item = astra_new_button_item(display_name, on_network_button_pressed, default_icon);
        }

        astra_push_item_to_list(g_networks_list, item);
    }

    /* Append Scan button at the end */
    g_scan_button = astra_new_button_item("扫描", on_scan_pressed, default_icon);
    astra_push_item_to_list(g_networks_list, g_scan_button);

    /* After rebuild, if selector was on Networks, move it to the first child
       so the user sees the network list content immediately. */
    if (astra_selector.selected_item == g_networks_list && g_networks_list->child_num > 0) {
        astra_selector.selected_item = g_networks_list->child_list_item[0];
        astra_selector.selected_index = 0;
    }

    /* Free the scan result buffer so the next scan can start cleanly. */
    WiFi.scanDelete();
}

/* ─── Callbacks ─── */

static void on_network_button_pressed(void)
{
    const char *content = astra_selector.selected_item->content;
    if (!content) {
        return;
    }

    astra_push_pop_up("请在串口输入密码", 100);
    serial_request_wifi_password(content);
    g_state = WIFI_MGR_CONNECTING;
}

static void on_reconnect_pressed(void)
{
    const char *parent_content = astra_selector.selected_item->parent->content;
    if (!parent_content) {
        return;
    }

    /* Strip trailing '*' marker */
    char ssid[STORAGE_SSID_MAX_LEN];
    strlcpy(ssid, parent_content, sizeof(ssid));
    size_t len = strlen(ssid);
    if (len > 0 && ssid[len - 1] == '*') {
        ssid[len - 1] = '\0';
    }

    char pass[STORAGE_PASS_MAX_LEN];
    int idx = storage_wifi_find(ssid);
    if (idx < 0) {
        return;
    }
    storage_wifi_get(idx, ssid, pass);

    WiFi.disconnect();
    WiFi.begin(ssid, pass);
    strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
    g_connecting = true;
    astra_push_pop_up("连接中...", 3000);

    astra_selector_exit_current_item();
}

static void on_delete_pressed(void)
{
    const char *parent_content = astra_selector.selected_item->parent->content;
    if (!parent_content) {
        return;
    }

    /* Strip trailing '*' marker */
    char ssid[STORAGE_SSID_MAX_LEN];
    strlcpy(ssid, parent_content, sizeof(ssid));
    size_t len = strlen(ssid);
    if (len > 0 && ssid[len - 1] == '*') {
        ssid[len - 1] = '\0';
    }

    int idx = storage_wifi_find(ssid);
    if (idx >= 0) {
        storage_wifi_remove(idx);
    }

    astra_push_pop_up("已删除", 1500);
    astra_selector_exit_current_item();
    rebuild_network_list(WiFi.scanComplete());
}

static void on_scan_pressed(void)
{
    WiFi.scanDelete();          /* free any stale results first */
    int16_t scan_ret = WiFi.scanNetworks(true);    /* async */
    if (scan_ret == -2) {
        astra_push_pop_up("扫描失败", 2000);
        g_state = WIFI_MGR_IDLE;
    } else {
        g_state = WIFI_MGR_SCANNING;
        astra_push_pop_up("扫描中...", 100);
    }
}

/* ─── Per-frame update (non-blocking state machine) ─── */

#define WIFI_SCAN_MAX_RETRIES 3
static int g_scan_retry_count = 0;

void wifi_mgr_update(void)
{
    if (!g_wifi_enabled && g_state == WIFI_MGR_IDLE) {
        return;
    }

    switch (g_state) {

    case WIFI_MGR_SCANNING: {
        astra_push_pop_up("扫描中...", 100);
        int16_t result = WiFi.scanComplete();
        if (result >= 0) {
            rebuild_network_list(result);
            g_state = WIFI_MGR_SCAN_DONE;
            g_scan_retry_count = 0;
        } else if (result == -2) {
            /* 扫描失败 - retry if under limit */
            if (g_scan_retry_count < WIFI_SCAN_MAX_RETRIES) {
                g_scan_retry_count++;
                WiFi.scanNetworks(true);
            } else {
                astra_push_pop_up("扫描失败", 2000);
                g_state = WIFI_MGR_IDLE;
                g_scan_retry_count = 0;
            }
        }
        /* result == -1 => still scanning, wait */
        break;
    }

    case WIFI_MGR_CONNECTING: {
        /* Keep pop-up visible while waiting for input */
        astra_push_pop_up("请在串口输入密码", 100);
        /* Poll serial for password input */
        serial_state_t ss = serial_poll();
        if (ss == SERIAL_STATE_PASSWORD_RECEIVED) {
            const char *input  = serial_get_input();
            const char *target = serial_get_target_name();
            if (input && target) {
                WiFi.disconnect();
                WiFi.begin(target, input);
                strncpy(g_connecting_ssid, target, STORAGE_SSID_MAX_LEN);
                strncpy(g_connecting_pass, input,  STORAGE_PASS_MAX_LEN);
                g_connecting = true;
                Serial.println("CONNECTING...");
                astra_push_pop_up("连接中...", 3000);
            }
        } else if (ss == SERIAL_STATE_CANCELLED) {
            g_state = WIFI_MGR_SCAN_DONE;
            astra_push_pop_up("已取消", 1500);
        }

        /* Check WiFi connection status */
        if (g_connecting) {
            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                g_connecting = false;
                storage_wifi_add(g_connecting_ssid, g_connecting_pass);
                Serial.println("OK");
                astra_push_pop_up("已连接", 2000);
                g_state = WIFI_MGR_CONNECTED;
            } else if (status == WL_CONNECT_FAILED ||
                       status == WL_NO_SSID_AVAIL) {
                g_connecting = false;
                Serial.println("FAIL");
                astra_push_pop_up("连接失败", 2000);
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
