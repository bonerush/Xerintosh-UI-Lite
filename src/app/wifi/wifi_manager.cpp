/**
 * @file   wifi_manager.cpp
 * @brief  WiFi 管理器实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：基于 ESP32 WiFi 库的完整状态机实现，
 *            支持异步扫描、串口密码输入、已保存网络管理及 UI 菜单动态构建。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include "app/wifi/wifi_manager.h"

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

#include "app/wifi/wifi_manager.h"

extern "C" {
#include "app/storage/storage.h"
#include "app/serial_input/serial_input.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "kernel/kern_task.h"
}

/* ═══ 外部全局变量 ═══ */

extern bool wifi_on;   /* 定义在 main.cpp */

/* ═══ 模块状态 ═══ */

static wifi_mgr_state_t g_state           = WIFI_MGR_IDLE;    /* 状态机当前状态 */
static bool             g_wifi_enabled    = false;            /* WiFi 是否已启用 */

/* UI 菜单指针（用于动态构建/清理网络菜单） */
static xerintosh_list_item_t *g_settings_list      = NULL;  /* "设置" 列表项 */
static xerintosh_list_item_t *g_networks_list      = NULL;  /* "网络" 列表项（设置的子项） */
static xerintosh_list_item_t *g_saved_container    = NULL;  /* "已保存" 容器 */
static xerintosh_list_item_t *g_available_container = NULL; /* "可用网络" 容器 */
static xerintosh_list_item_t *g_scan_button        = NULL;  /* "扫描" 按钮 */

/* 连接状态 */
static bool  g_connecting = false;                              /* 是否正在连接 */
static char  g_connecting_ssid[STORAGE_SSID_MAX_LEN] = {0};     /* 正在连接的 SSID */
static char  g_connecting_pass[STORAGE_PASS_MAX_LEN] = {0};     /* 正在连接的密码 */

/* 时序控制 */
static unsigned long g_wifi_scan_start_time = 0;   /* 扫描开始时间 */
static unsigned long g_warmup_start_time   = 0;    /* 预热开始时间 */
static unsigned long g_connect_start_time  = 0;    /* 连接开始时间 */
static int g_scan_retry_count = 0;                 /* 扫描重试计数 */

/* 超时常量 */
#define WIFI_WARMUP_DELAY_MS    100   /* 预热等待时间 */
#define WIFI_SCAN_TIMEOUT_MS    30000  /* 扫描超时时间 */
#define WIFI_CONNECT_TIMEOUT_MS 15000  /* 连接超时时间 */

/* ═══ 前向声明（回调函数）═══ */

static void on_network_button_pressed(void);
static void on_saved_connect_pressed(void);
static void on_saved_delete_pressed(void);
static void on_scan_pressed(void);
static void rebuild_network_list(int scan_count);
static void suppress_wifi_logs(void);
static void restore_wifi_logs(void);
extern "C" void wifi_mgr_task_main(void *arg);

/* ═══ 公共查询接口 ═══ */

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

/* ═══ 初始化 ═══ */

/**
 * @brief 初始化 WiFi 管理器
 * @note  获取设置菜单指针，用于后续动态挂载网络子菜单。
 *        注意：不在此处自动连接，避免 WiFi.disconnect() 导致驱动不稳定。
 */
void wifi_mgr_init(void)
{
    /* 设置是根节点的第一个子项 */
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root && root->child_num > 0) {
        g_settings_list = root->child_list_item[0];  /* "设置" */
    }

    /* 注意：自动连接已移除。WiFi 默认关闭（wifi_on = false）。
       在此处调用 WiFi.begin() 会导致后续扫描失败，因为
       WiFi.disconnect() 会使驱动处于不稳定状态。
       连接逻辑改为用户选择网络时触发。 */
}

/* ═══ 启用 / 禁用 ═══ */

/**
 * @brief 启用 WiFi：进入 STA 模式，开始预热，并重建网络菜单
 */
void wifi_mgr_enable(void)
{
    g_wifi_enabled = true;
    WiFi.mode(WIFI_STA);
    g_warmup_start_time = millis();
    g_state = WIFI_MGR_WARMUP;
    rebuild_network_list(0);
}

/* ─── 日志抑制辅助 ─── */

/**
 * @brief 抑制 WiFi 驱动日志（连接期间减少串口输出）
 */
static void suppress_wifi_logs(void)
{
    esp_log_level_set("wifi", ESP_LOG_ERROR);
}

/**
 * @brief 恢复 WiFi 驱动日志级别
 */
static void restore_wifi_logs(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
}

/**
 * @brief 禁用 WiFi：断开连接、关闭驱动、清理菜单
 */
void wifi_mgr_disable(void)
{
    if (g_connecting) {
        restore_wifi_logs();
    }
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    g_wifi_enabled = false;

    if (g_networks_list) {
        /* 若选择器当前位于网络子树内，将其移回设置项 */
        xerintosh_list_item_t *check = g_xerintosh_selector.selected_item;
        while (check && check != g_networks_list) {
            check = check->parent;
        }
        if (check == g_networks_list) {
            g_xerintosh_selector.selected_item  = g_settings_list->child_list_item[0];
            g_xerintosh_selector.selected_index = 0;
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

/* ═══ 开关切换回调 ═══ */

/**
 * @brief WiFi 开关切换回调（由 switch_item 的 exit_function 调用）
 */
void wifi_mgr_on_switch_toggle(void)
{
    if (wifi_on) {
        wifi_mgr_enable();
    } else {
        wifi_mgr_disable();
    }
}

/* ═══ 网络菜单重建 ═══ */

/**
 * @brief 重建网络子菜单（已保存 + 可用网络 + 扫描按钮）
 * @param scan_count 扫描结果数量（可用网络条目数）
 * @note  每次扫描完成或启用 WiFi 时调用，动态更新菜单内容
 */
static void rebuild_network_list(int scan_count)
{
    /* 首次调用时创建网络列表项 */
    if (!g_networks_list) {
        g_networks_list = xerintosh_new_list_item("网络", list_icon);
        if (g_settings_list && g_networks_list) {
            xerintosh_push_item_to_list(g_settings_list, g_networks_list);
        }
    }

    if (!g_networks_list) {
        return;
    }

    /* 安全处理：若选择器位于网络子树内，先将其移到网络项本身，
       避免清理子项后产生悬空指针。 */
    xerintosh_list_item_t *check = g_xerintosh_selector.selected_item;
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
        g_xerintosh_selector.selected_item = g_networks_list;
        g_xerintosh_selector.selected_index = idx;
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

    /* ─── 2. 可用网络容器（仅显示未保存的扫描结果）─── */
    g_available_container = xerintosh_new_list_item("可用网络", list_icon);
    xerintosh_push_item_to_list(g_networks_list, g_available_container);

    int show_count = scan_count;
    if (show_count > 9) {
        show_count = 9;
    }
    for (int i = 0; i < show_count; i++) {
        String ssid = WiFi.SSID(i);
        if (storage_wifi_find(ssid.c_str()) >= 0) {
            continue; /* 跳过已保存网络 */
        }
        xerintosh_list_item_t *item = xerintosh_new_button_item(ssid.c_str(), on_network_button_pressed, default_icon);
        xerintosh_push_item_to_list(g_available_container, item);
    }

    /* ─── 3. 扫描按钮 ─── */
    g_scan_button = xerintosh_new_button_item("扫描", on_scan_pressed, default_icon);
    xerintosh_push_item_to_list(g_networks_list, g_scan_button);

    /* 重建后，若选择器位于网络项上，将其移到第一个子项，
       使用户立即看到网络列表内容。 */
    if (g_xerintosh_selector.selected_item == g_networks_list && g_networks_list->child_num > 0) {
        g_xerintosh_selector.selected_item = g_networks_list->child_list_item[0];
        g_xerintosh_selector.selected_index = 0;
    }

    /* 释放扫描结果缓冲区，确保下次扫描可以干净启动 */
    WiFi.scanDelete();
}

/* ═══ 回调函数 ═══ */

/**
 * @brief 可用网络按钮按下回调：请求串口输入密码并进入连接状态
 */
static void on_network_button_pressed(void)
{
    const char *content = g_xerintosh_selector.selected_item->content;
    if (!content) {
        return;
    }

    xerintosh_push_pop_up("请在串口输入密码", 8000);
    serial_request_wifi_password(content);
    g_state = WIFI_MGR_CONNECTING;
}

/**
 * @brief 已保存网络"连接"按钮按下回调：读取密码并直接连接
 */
static void on_saved_connect_pressed(void)
{
    const char *content = g_xerintosh_selector.selected_item->parent->content;
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

/**
 * @brief 已保存网络"删除"按钮按下回调
 */
static void on_saved_delete_pressed(void)
{
    const char *content = g_xerintosh_selector.selected_item->parent->content;
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

/**
 * @brief 扫描按钮按下回调：启动异步网络扫描
 */
static void on_scan_pressed(void)
{
    if (g_connecting) {
        WiFi.disconnect();
        restore_wifi_logs();
        g_connecting = false;
    }
    WiFi.scanDelete();          /* 先释放过期结果 */
    int16_t scan_ret = WiFi.scanNetworks(true);    /* 异步扫描 */
    if (scan_ret == -2) {
        xerintosh_push_pop_up("扫描失败", 2000);
        g_state = WIFI_MGR_IDLE;
    } else if (scan_ret >= 0) {
        /* 同步扫描完成 */
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

/* ═══ 每帧更新（非阻塞状态机）═══ */

#define WIFI_SCAN_MAX_RETRIES 3  /* 扫描失败最大重试次数 */

/**
 * @brief 每帧更新 WiFi 状态机（非阻塞）
 * @note  处理预热倒计时、扫描超时/完成、连接超时/成功/失败等状态转换
 */
void wifi_mgr_update(void)
{
    if (!g_wifi_enabled && g_state == WIFI_MGR_IDLE) {
        return;
    }

    switch (g_state) {

    case WIFI_MGR_WARMUP: {
        /* 预热完成后不自动扫描，等待用户手动点击"扫描"按钮 */
        if (millis() - g_warmup_start_time >= WIFI_WARMUP_DELAY_MS) {
            g_state = WIFI_MGR_CONNECTED;
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
            /* 扫描失败 - 若未超重试限制则重试 */
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
        /* result == -1 => 仍在扫描中，继续等待 */
        break;
    }

    case WIFI_MGR_CONNECTING: {
        /* 轮询串口密码输入 */
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

        /* 检查 WiFi 连接状态 */
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
            /* WL_IDLE_STATUS / WL_DISCONNECTED => 仍在尝试中 */
        }
        break;
    }

    case WIFI_MGR_CONNECTED:
    case WIFI_MGR_CONNECT_FAILED:
        /* 保持当前状态直到用户采取新动作（重新扫描、选择其他网络） */
        break;

    default:
        break;
    }
}

/* ═══ 内核任务入口 ═══ */

/**
 * @brief WiFi 管理器内核任务入口
 * @note  每 50ms 轮询一次状态机。
 */
extern "C" void wifi_mgr_task_main(void *arg)
{
    (void)arg;
    for (;;) {
        wifi_mgr_update();
        kern_sleep_ms(50);
    }
}

#endif /* NATIVE_TEST */
