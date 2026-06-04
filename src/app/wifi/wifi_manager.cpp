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
bool wifi_mgr_is_waiting_input(void) { return false; }
bool wifi_mgr_is_enabled(void) { return false; }
void wifi_mgr_update(void) {}
void wifi_mgr_on_switch_toggle(void *ud) { (void)ud; }
extern "C" void wifi_popup_refresh(void) {}
void wifi_mgr_task_main(void *arg) { (void)arg; }

#else

#include <WiFi.h>
#include <Arduino.h>
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>

#include "app/wifi/wifi_manager.h"

extern "C" {
#include "app/storage/storage.h"
#include "app/serial_input/serial_input.h"
#include "app/settings/settings.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "kernel/kern_task.h"
}

/* ═══ 外部全局变量 ═══ */

extern bool g_wifi_on;   /* 定义在 main.cpp */

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
static bool g_initial_scan_shown = false;           /* 首次启动扫描弹窗是否已显示 */
static bool g_auto_connect_done = false;            /* 本次 enable 周期内是否已尝试自动连接 */
static bool g_is_auto_connect   = false;            /* 当前连接是否为自动连接（抑制弹窗） */

/* ESP-IDF 异步扫描回调标志 */
static volatile bool g_scan_done = false;
static volatile uint16_t g_scan_ap_count = 0;

/* ESP-IDF 扫描完成回调（WiFi 事件驱动）
 * 只设标志，不读取 AP 数量——Arduino WiFi 库的内部处理器先于回调执行，
 * 会消费掉 esp_wifi_scan_get_ap_records 的内部缓冲区。
 * 改为在 wifi_mgr_update() 中用 esp_wifi_scan_get_ap_records() 一次性读取。 */
static void wifi_scan_done_handler(void* arg, esp_event_base_t base,
                                   int32_t event_id, void* data)
{
    (void)arg; (void)base; (void)event_id; (void)data;
    g_scan_done = true;
}

/* 超时常量 */
#define WIFI_WARMUP_DELAY_MS    2000  /* 预热等待时间（WiFi 驱动需要充分初始化） */
#define WIFI_SCAN_TIMEOUT_MS    30000  /* 扫描超时时间 */
#define WIFI_CONNECT_TIMEOUT_MS 15000  /* 连接超时时间 */

/* ═══ 跨任务弹窗辅助 ═══
 * 与 power_key_popup 同模式：UI 任务每帧 push_pop_up 保持弹窗存活，
 * 自行管理超时退场（span 到期后 dismiss_pop_up 触发动画退出）。 */

static volatile bool     g_popup_active = false;   /* 弹窗是否激活 */
static volatile uint16_t g_popup_span   = 0;        /* 显示时长（毫秒） */
static uint32_t          g_popup_start  = 0;        /* 弹窗激活时的 tick */
static char              g_popup_content[48] = {0};  /* 弹窗文本 */

/**
 * @brief 请求显示弹窗（可从任意任务调用）
 * @note  设置激活标志，UI 任务每帧 push 保持弹窗存活，span 到期自动退场。
 */
static void wifi_popup_request(const char *msg, uint16_t span_ms)
{
    strncpy(g_popup_content, msg, sizeof(g_popup_content) - 1);
    g_popup_content[sizeof(g_popup_content) - 1] = '\0';
    g_popup_span = span_ms;
    g_popup_start = millis();
    g_popup_active = true;
}

/**
 * @brief 提前关闭弹窗（触发动画退出）
 */
static void wifi_popup_dismiss(void)
{
    g_popup_active = false;
    xerintosh_dismiss_pop_up();
}

/**
 * @brief UI 任务每帧调用：保持弹窗存活并管理超时退场
 * @note  由 app_input_process() 在 UI 任务中调用。
 *        激活期间每帧 push_pop_up（与 power_key_popup 同模式），
 *        span 到期后调用 dismiss_pop_up 触发向上滑出动画。
 */
extern "C" void wifi_popup_refresh(void)
{
    if (!g_popup_active) return;

    /* 超时检查：span 到期后触发动画退场 */
    if (millis() - g_popup_start >= g_popup_span) {
        wifi_popup_dismiss();
        return;
    }

    /* 每帧 push 保持弹窗存活（重置 time_start 防止弹窗自身超时） */
    xerintosh_push_pop_up(g_popup_content, g_popup_span);
}

/* ═══ 前向声明（回调函数）═══ */

static void on_network_button_pressed(void *ud);
static void on_saved_connect_pressed(void *ud);
static void on_saved_delete_pressed(void *ud);
static void on_scan_pressed(void *ud);
static void rebuild_network_list(int scan_count);
static bool try_auto_connect(void);
static void suppress_wifi_logs(void);
static void restore_wifi_logs(void);
extern "C" void wifi_mgr_task_main(void *arg);

/* ═══ 公共查询接口 ═══ */

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

    /* 注意：自动连接已移除。WiFi 默认关闭（g_wifi_on = false）。
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

    /* ── 内存预检：WiFi 驱动初始化需要 ~40KB 堆 ──
     * BT 默认关闭时堆约 163KB，BT 按需加载后堆约 132KB，仍需留足余量 */
    if (ESP.getFreeHeap() < 45000) {
        wifi_popup_request("内存不足", 2000);
        g_wifi_enabled = false;
        g_state = WIFI_MGR_IDLE;
        rebuild_network_list(0);
        return;
    }

    WiFi.persistent(false);

    /* 使用标准 Arduino WiFi API 初始化驱动 */
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* 注册 ESP-IDF 扫描完成事件回调（Arduino WiFi.scanNetworks 在非主任务中不工作） */
    g_scan_done = false;
    g_scan_ap_count = 0;
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                               wifi_scan_done_handler, NULL);

    g_warmup_start_time = millis();
    g_state = WIFI_MGR_WARMUP;
    g_auto_connect_done = false;
    g_is_auto_connect = false;
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
    wifi_popup_dismiss();
    if (g_connecting) {
        restore_wifi_logs();
    }
    /* 注销扫描完成事件回调 */
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                 wifi_scan_done_handler);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_wifi_enabled = false;

    if (g_networks_list) {
        /* 若选择器当前位于网络子树内，将其移回设置项 */
        ui_selector_safety_move_out(g_networks_list, g_settings_list);

        xerintosh_clear_children_of_list(g_networks_list);
        xerintosh_remove_item_from_list(g_settings_list, g_networks_list);
        g_networks_list = NULL;
    }

    g_saved_container     = NULL;
    g_available_container = NULL;
    g_scan_button         = NULL;
    g_connecting          = false;
    g_state               = WIFI_MGR_IDLE;
    g_auto_connect_done   = false;
    g_is_auto_connect     = false;
}

/* ═══ 开关切换回调 ═══ */

/**
 * @brief WiFi 开关切换回调（由 switch_item 的 exit_function 调用）
 */
void wifi_mgr_on_switch_toggle(void *ud)
{
    (void)ud;
    if (g_wifi_on) {
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

    /* 安全处理：若选择器位于网络子树内，先将其移到网络项本身 */
    ui_selector_rebuild_anchor(g_networks_list, g_settings_list);

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
        if (ssid.length() == 0) continue; /* 跳过隐藏网络 */
        if (storage_wifi_find(ssid.c_str()) >= 0) {
            continue; /* 跳过已保存网络 */
        }
        xerintosh_list_item_t *item = xerintosh_new_button_item(ssid.c_str(), on_network_button_pressed, default_icon);
        xerintosh_push_item_to_list(g_available_container, item);
    }

    /* ─── 3. 扫描按钮 ─── */
    g_scan_button = xerintosh_new_button_item("扫描", on_scan_pressed, default_icon);
    xerintosh_push_item_to_list(g_networks_list, g_scan_button);

    /* 重建后，若选择器位于网络项上，根据上下文移到合适的子项：
       - 有可用网络 → 移到第一个可用网络（用户刚扫描完，想看结果）
       - 无可用网络 → 移到 "已保存"（第一个子项） */
    if (g_xerintosh_selector.selected_item == g_networks_list && g_networks_list->child_num > 0) {
        if (g_available_container && g_available_container->child_num > 0) {
            g_xerintosh_selector.selected_item = g_available_container->child_list_item[0];
            g_xerintosh_selector.selected_index = 0;
        } else {
            g_xerintosh_selector.selected_item = g_networks_list->child_list_item[0];
            g_xerintosh_selector.selected_index = 0;
        }
    }
}

/* ═══ 回调函数 ═══ */

/**
 * @brief 可用网络按钮按下回调：请求串口输入密码并进入连接状态
 */
static void on_network_button_pressed(void *ud)
{
    (void)ud;
    const char *content = g_xerintosh_selector.selected_item->content;
    if (!content) {
        return;
    }

    wifi_popup_request("请在串口输入密码", 8000);
    serial_request_wifi_password(content);
    g_is_auto_connect = false;
    g_state = WIFI_MGR_CONNECTING;
}

/**
 * @brief 已保存网络"连接"按钮按下回调：读取密码并直接连接
 */
static void on_saved_connect_pressed(void *ud)
{
    (void)ud;
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
    delay(1);  /* 喂狗：BT 活跃时 WiFi 操作可能阻塞 */
    WiFi.begin(ssid, pass);
    strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
    strncpy(g_connecting_pass, pass,  STORAGE_PASS_MAX_LEN);
    g_connecting = true;
    g_connect_start_time = millis();
    g_is_auto_connect = false;
    g_state = WIFI_MGR_CONNECTING;
    wifi_popup_request("连接中...", 15000);

    xerintosh_selector_exit_current_item();
}

/**
 * @brief 已保存网络"删除"按钮按下回调
 */
static void on_saved_delete_pressed(void *ud)
{
    (void)ud;
    const char *content = g_xerintosh_selector.selected_item->parent->content;
    if (!content) {
        return;
    }

    int idx = storage_wifi_find(content);
    if (idx >= 0) {
        storage_wifi_remove(idx);
    }

    wifi_popup_request("已删除", 1500);
    xerintosh_selector_exit_current_item();
    rebuild_network_list(0);
}

/**
 * @brief 扫描按钮按下回调：启动异步网络扫描
 */
static void on_scan_pressed(void *ud)
{
    (void)ud;
    if (g_connecting) {
        WiFi.disconnect();
        restore_wifi_logs();
        g_connecting = false;
    }

    g_scan_done = false;
    g_scan_ap_count = 0;
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = true;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        wifi_popup_request("扫描失败", 2000);
        g_state = WIFI_MGR_SCAN_DONE;
    } else {
        g_state = WIFI_MGR_SCANNING;
        g_wifi_scan_start_time = millis();
        wifi_popup_request("扫描中...", WIFI_SCAN_TIMEOUT_MS);
    }
}

/* ═══ 自动连接 ═══ */

/**
 * @brief 扫描完成后尝试自动连接到最佳已保存网络
 * @return true  如果发起了自动连接
 * @note  单个已保存网络直接连接；多个已保存网络选择扫描结果中 RSSI 最强的。
 *        不显示弹窗（静默连接）。
 */
static bool try_auto_connect(void)
{
    int saved_count = storage_wifi_get_count();
    if (saved_count <= 0) return false;

    int16_t scan_count = WiFi.scanComplete();
    if (scan_count <= 0) return false;

    /* 在扫描结果中查找已保存网络，选择信号最强的 */
    int best_saved_idx = -1;
    int8_t best_rssi = -128;

    for (int i = 0; i < saved_count; i++) {
        char ssid[STORAGE_SSID_MAX_LEN];
        char pass[STORAGE_PASS_MAX_LEN];
        if (!storage_wifi_get(i, ssid, pass)) continue;

        for (int j = 0; j < scan_count; j++) {
            String scanned = WiFi.SSID(j);
            if (scanned.length() == 0) continue;
            if (scanned.equals(ssid)) {
                int8_t rssi = WiFi.RSSI(j);
                if (rssi > best_rssi) {
                    best_rssi = rssi;
                    best_saved_idx = i;
                }
                break;
            }
        }
    }

    if (best_saved_idx < 0) return false;

    /* 发起静默连接 */
    char ssid[STORAGE_SSID_MAX_LEN];
    char pass[STORAGE_PASS_MAX_LEN];
    if (!storage_wifi_get(best_saved_idx, ssid, pass)) return false;

    suppress_wifi_logs();
    WiFi.disconnect();
    delay(1);
    WiFi.begin(ssid, pass);
    strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
    strncpy(g_connecting_pass, pass,  STORAGE_PASS_MAX_LEN);
    g_connecting = true;
    g_connect_start_time = millis();
    g_is_auto_connect = true;
    g_state = WIFI_MGR_CONNECTING;
    return true;
}

/* ═══ 每帧更新（非阻塞状态机）═══ */

/**
 * @brief 每帧更新 WiFi 状态机（非阻塞）
 * @note  处理预热倒计时、扫描超时/完成、连接超时/成功/失败等状态转换
 */
bool wifi_mgr_is_enabled(void) {
    return g_wifi_enabled;
}

void wifi_mgr_update(void)
{
    if (!g_wifi_enabled && g_state == WIFI_MGR_IDLE) {
        return;
    }

    switch (g_state) {

    case WIFI_MGR_WARMUP: {
        /* 预热完成后启动 ESP-IDF 异步扫描 */
        if (millis() - g_warmup_start_time >= WIFI_WARMUP_DELAY_MS) {
            g_scan_done = false;
            g_scan_ap_count = 0;
            wifi_scan_config_t scan_cfg = {};
            scan_cfg.show_hidden = true;
            esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
            if (err != ESP_OK) {
                if (!g_initial_scan_shown) {
                    wifi_popup_request("扫描失败", 2000);
                }
                g_state = WIFI_MGR_SCAN_DONE;
                g_initial_scan_shown = true;
            } else {
                g_state = WIFI_MGR_SCANNING;
                g_wifi_scan_start_time = millis();
                if (!g_initial_scan_shown) {
                    wifi_popup_request("扫描中...", WIFI_SCAN_TIMEOUT_MS);
                }
            }
        }
        break;
    }

    case WIFI_MGR_SCANNING: {
        /* 扫描超时检查 */
        if (millis() - g_wifi_scan_start_time >= WIFI_SCAN_TIMEOUT_MS) {
            if (!g_initial_scan_shown) {
                wifi_popup_dismiss();
            }
            rebuild_network_list(0);
            g_state = WIFI_MGR_SCAN_DONE;
            g_initial_scan_shown = true;
            break;
        }

        if (g_scan_done) {
            /* 回调已触发。Arduino WiFi 库内部已缓存扫描结果，
             * 用 WiFi.scanComplete() 读取缓存的数量。 */
            int16_t result = WiFi.scanComplete();
            if (!g_initial_scan_shown) {
                wifi_popup_dismiss();
            }
            rebuild_network_list(result >= 0 ? result : 0);
            if (result > 0 && !g_initial_scan_shown) {
                wifi_popup_request("扫描完毕", 1500);
            }
            g_state = WIFI_MGR_SCAN_DONE;
            g_initial_scan_shown = true;
        }
        /* else: 仍在扫描中，等待回调 */
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
                wifi_popup_request("连接中...", 15000);
            }
        } else if (ss == SERIAL_STATE_CANCELLED) {
            restore_wifi_logs();
            g_connecting = false;
            g_state = WIFI_MGR_SCAN_DONE;
            wifi_popup_dismiss();
        }

        /* 检查 WiFi 连接状态 */
        if (g_connecting) {
            /* 连接超时检查 */
            if (millis() - g_connect_start_time >= WIFI_CONNECT_TIMEOUT_MS) {
                WiFi.disconnect();
                restore_wifi_logs();
                g_connecting = false;
                if (!g_is_auto_connect) {
                    wifi_popup_request("连接超时", 2000);
                }
                g_state = WIFI_MGR_CONNECT_FAILED;
                break;
            }

            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                restore_wifi_logs();
                g_connecting = false;
                storage_wifi_add(g_connecting_ssid, g_connecting_pass);
                if (!g_is_auto_connect) {
                    wifi_popup_request("已连接", 1500);
                }
                g_state = WIFI_MGR_CONNECTED;
                rebuild_network_list(0);
            } else if (status == WL_CONNECT_FAILED ||
                       status == WL_NO_SSID_AVAIL) {
                restore_wifi_logs();
                g_connecting = false;
                if (!g_is_auto_connect) {
                    wifi_popup_request("连接失败", 2000);
                }
                g_state = WIFI_MGR_CONNECT_FAILED;
            }
            /* WL_IDLE_STATUS / WL_DISCONNECTED => 仍在尝试中 */
        }
        break;
    }

    case WIFI_MGR_SCAN_DONE: {
        /* 扫描完成后尝试自动连接（仅一次） */
        if (!g_auto_connect_done) {
            g_auto_connect_done = true;
            try_auto_connect();
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
    kern_poll_loop(wifi_mgr_update, 50);
}

#endif /* NATIVE_TEST */
