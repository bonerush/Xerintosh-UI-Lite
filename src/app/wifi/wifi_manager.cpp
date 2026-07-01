/**
 * @file   wifi_manager.cpp
 * @brief  WiFi 管理器实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：基于 ESP-IDF WiFi 库的完整状态机实现，
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
bool wifi_mgr_is_driver_on(void) { return false; }
bool wifi_mgr_is_connected(void) { return false; }
void wifi_mgr_ensure_dns(void) {}
uint32_t wifi_mgr_needed_heap(void) { return 45000; }
void wifi_mgr_update(void) {}
void wifi_mgr_request_enable(void) {}
void wifi_mgr_request_disable(void) {}
void wifi_mgr_process_requests(void) {}
void wifi_mgr_on_switch_toggle(void *ud) { (void)ud; }
extern "C" void wifi_popup_refresh(void) {}
void wifi_mgr_task_main(void *arg) { (void)arg; }

#else

#include <string.h>
#include <inttypes.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <lwip/inet.h>
#include "hal/hal_system.h"

#define TAG "wifi_mgr"

#include "app/wifi/wifi_manager.h"
#include "app/wifi/wifi_menu.h"
#include "app/app_mem.h"

extern "C" {
#include "app/storage/storage.h"
#include "app/serial_input/serial_input.h"
#include "app/settings/settings.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "kernel/kern_task.h"
#include "kernel/kern_sync.h"
}

/* ═══ 外部全局变量 ═══ */

#include "app/app_state.h"

/* ═══ 模块状态 ═══ */

static xeros_spinlock_t g_popup_spinlock;

static wifi_mgr_state_t g_state           = WIFI_MGR_IDLE;
static bool             g_wifi_enabled    = false;
static bool             g_wifi_driver_inited = false;

/* ═══ 异步请求标志（线程安全）═══ */
static volatile bool g_enable_requested  = false;
static volatile bool g_disable_requested = false;

/* 连接状态 */
static bool  g_connecting = false;
static char  g_connecting_ssid[STORAGE_SSID_MAX_LEN] = {0};
static char  g_connecting_pass[STORAGE_PASS_MAX_LEN] = {0};

/* 时序控制 */
static uint32_t g_wifi_scan_start_time = 0;
static uint32_t g_warmup_start_time   = 0;
static uint32_t g_connect_start_time  = 0;
static bool g_initial_scan_shown = false;
static bool g_auto_connect_done = false;
static bool g_is_auto_connect   = false;

/* ESP-IDF 异步扫描结果缓存 */
static volatile bool g_scan_done = false;
static uint16_t g_scan_ap_count = 0;
static wifi_ap_record_t *g_scan_ap_records = NULL;
#define WIFI_SCAN_MAX_AP 32

/* ═══ 事件驱动连接标志（由 WiFi/IP 事件处理器设置，update loop 消费）═══ */
static volatile bool g_evt_sta_connected    = false;  /* WIFI_EVENT_STA_CONNECTED 已触发 */
static volatile bool g_evt_sta_disconnected = false;  /* WIFI_EVENT_STA_DISCONNECTED 已触发 */
static volatile bool g_evt_got_ip           = false;  /* IP_EVENT_STA_GOT_IP 已触发 */
static volatile uint8_t g_evt_disconnect_reason = 0;   /* 断开原因码 */

/* 自动重连控制 */
static uint8_t g_reconnect_attempts = 0;
#define WIFI_MAX_RECONNECT_ATTEMPTS 3

/* ═══ WiFi 调试日志辅助 ═══
 * 在关键查询路径上打印详细信息，帮助定位网络连接问题。
 * 使用 ESP_LOGI 确保默认日志级别下可见。 */
#define WIFI_LOGD(...) ESP_LOGI(TAG, __VA_ARGS__)

/**
 * @brief 格式化 IP 地址（避免依赖 ip4addr_ntoa 等 lwip 函数）
 */
static void ip_to_str(uint32_t addr, char *buf, size_t buf_size)
{
    if (!buf || buf_size < 1) return;
    uint8_t *b = (uint8_t *)&addr;
    snprintf(buf, buf_size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

/* ═══ 统一 WiFi + IP 事件处理器 ═══ */

static void wifi_event_handler(void* arg, esp_event_base_t base,
                               int32_t event_id, void *data)
{
    (void)arg;
    switch (event_id) {
    case WIFI_EVENT_SCAN_DONE:
        WIFI_LOGD("[EVT] WIFI_EVENT_SCAN_DONE");
        g_scan_done = true;
        break;
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *evt =
            (wifi_event_sta_connected_t *)data;
        WIFI_LOGD("[EVT] WIFI_EVENT_STA_CONNECTED: ssid='%.32s' ch=%d auth=%d",
                   evt->ssid, evt->channel, evt->authmode);
        g_evt_sta_connected = true;
        g_evt_sta_disconnected = false;
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *evt =
            (wifi_event_sta_disconnected_t *)data;
        g_evt_disconnect_reason = evt->reason;
        g_evt_sta_disconnected = true;
        g_evt_sta_connected = false;
        g_evt_got_ip = false;
        /* reason codes: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code */
        WIFI_LOGD("[EVT] WIFI_EVENT_STA_DISCONNECTED: ssid='%.32s' reason=%d",
                   evt->ssid, evt->reason);
        break;
    }
    case WIFI_EVENT_STA_START:
        WIFI_LOGD("[EVT] WIFI_EVENT_STA_START");
        break;
    case WIFI_EVENT_STA_STOP:
        WIFI_LOGD("[EVT] WIFI_EVENT_STA_STOP");
        break;
    default:
        WIFI_LOGD("[EVT] unhandled wifi event: %" PRId32 " (base=%s)",
                   event_id, base ? base : "?");
        break;
    }
}

static void ip_event_handler(void* arg, esp_event_base_t base,
                             int32_t event_id, void *data)
{
    (void)arg;
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        if (evt && evt->ip_info.ip.addr != 0) {
            char ip_buf[16];
            ip_to_str(evt->ip_info.ip.addr, ip_buf, sizeof(ip_buf));
            char gw_buf[16];
            ip_to_str(evt->ip_info.gw.addr, gw_buf, sizeof(gw_buf));
            WIFI_LOGD("[EVT] ✅ IP_EVENT_STA_GOT_IP: IP=%s GW=%s", ip_buf, gw_buf);
        } else {
            WIFI_LOGD("[EVT] IP_EVENT_STA_GOT_IP (no IP data)");
        }
        g_evt_got_ip = true;
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        WIFI_LOGD("[EVT] ⚠️ IP_EVENT_STA_LOST_IP");
    }
}

/* 超时常量 */
#define WIFI_WARMUP_DELAY_MS    2000
#define WIFI_SCAN_TIMEOUT_MS    30000
#define WIFI_CONNECT_TIMEOUT_MS 15000

/* 内存守卫阈值 */
#define WIFI_MIN_FREE_HEAP        45000
#define WIFI_MIN_MAX_ALLOC_HEAP   20000

/* ═══ 跨任务弹窗辅助 ═══
 * 与 power_key_popup 同模式：UI 任务每帧 push_pop_up 保持弹窗存活，
 * 自行管理超时退场（span 到期后 dismiss_pop_up 触发动画退出）。
 * g_popup_content 使用 xeros_spinlock_t 保护跨任务读写。 */

static volatile bool     g_popup_active = false;   /* 弹窗是否激活 */
static volatile uint16_t g_popup_span   = 0;        /* 显示时长（毫秒） */
static uint32_t          g_popup_start  = 0;        /* 弹窗激活时的 tick */
static char              g_popup_content[48] = {0};  /* 弹窗文本 */

/* ═══ InfoBar 状态（与弹窗并行，用于过程性通知，暂不使用） ═══ */

static volatile bool     g_infobar_active = false;  /* InfoBar 是否激活 */
static volatile uint16_t g_infobar_span   = 0;       /* 显示时长（毫秒） */
static uint32_t          g_infobar_start  = 0;       /* InfoBar 激活时的 tick */
static char              g_infobar_content[48] = {0}; /* InfoBar 文本 */

/**
 * @brief 请求显示弹窗（可从任意任务调用）
 * @note  设置激活标志，UI 任务每帧 push 保持弹窗存活，span 到期自动退场。
 *        弹窗比 InfoBar 重要，请求弹窗时自动关闭 InfoBar。
 */
static void wifi_popup_request(const char *msg, uint16_t span_ms)
{
    g_infobar_active = false;

    xeros_spinlock_lock(&g_popup_spinlock);
    strncpy(g_popup_content, msg, sizeof(g_popup_content) - 1);
    g_popup_content[sizeof(g_popup_content) - 1] = '\0';
    g_popup_span = span_ms;
    xeros_spinlock_unlock(&g_popup_spinlock);
    g_popup_start = hal_get_ticks();
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
 * @brief 请求显示 InfoBar（可从任意任务调用）
 * @note  InfoBar 不像弹窗需要用户注意，使用短 span + 每帧 push 模式。
 */
static void wifi_infobar_request(const char *msg, uint16_t span_ms)
{
    xeros_spinlock_lock(&g_popup_spinlock);
    strncpy(g_infobar_content, msg, sizeof(g_infobar_content) - 1);
    g_infobar_content[sizeof(g_infobar_content) - 1] = '\0';
    g_infobar_span = span_ms;
    xeros_spinlock_unlock(&g_popup_spinlock);
    g_infobar_start = hal_get_ticks();
    g_infobar_active = true;
}

/**
 * @brief 提前关闭 InfoBar
 */
static void wifi_infobar_dismiss(void)
{
    g_infobar_active = false;
}

/**
 * @brief UI 任务每帧调用：保持弹窗和 InfoBar 存活并管理超时退场
 * @note  由 app_input_process() 在 UI 任务中调用。
 *        弹窗激活期间每帧 push_pop_up，InfoBar 激活期间每帧 push_info_bar。
 *        两者并行且互不干扰：弹窗居中显示，InfoBar 为顶部信息栏。
 */
extern "C" void wifi_popup_refresh(void)
{
    /* ── 弹窗处理 ── */
    {
        /* 在锁内读取快照，防止与 wifi_popup_request() 竞争 */
        xeros_spinlock_lock(&g_popup_spinlock);
        bool active = g_popup_active;
        uint32_t start = g_popup_start;
        uint16_t span = g_popup_span;
        xeros_spinlock_unlock(&g_popup_spinlock);

        if (active) {
            /* 超时检查：span 到期后触发动画退场 */
            if (hal_get_ticks() - start >= span) {
                wifi_popup_dismiss();
            } else {
                /* 每帧 push 保持弹窗存活（重置 time_start 防止弹窗自身超时） */
                xeros_spinlock_lock(&g_popup_spinlock);
                xerintosh_push_pop_up(g_popup_content, span);
                xeros_spinlock_unlock(&g_popup_spinlock);
            }
        }
    }

    /* ── InfoBar 处理（与弹窗并行，每帧 push_info_bar） ── */
    {
        xeros_spinlock_lock(&g_popup_spinlock);
        bool active = g_infobar_active;
        uint32_t start = g_infobar_start;
        uint16_t span = g_infobar_span;
        xeros_spinlock_unlock(&g_popup_spinlock);

        if (!active) return;

        /* 超时检查：span 到期后自动停止 */
        if (hal_get_ticks() - start >= span) {
            wifi_infobar_dismiss();
            return;
        }

        /* 每帧 push 保持 InfoBar 存活 */
        xeros_spinlock_lock(&g_popup_spinlock);
        xerintosh_push_info_bar(g_infobar_content, span);
        xeros_spinlock_unlock(&g_popup_spinlock);
    }
}

/* ═══ 前向声明 ═══ */

static bool try_auto_connect(void);
extern "C" void wifi_mgr_task_main(void *arg);

/* ═══ 公共查询接口 ═══ */

bool wifi_mgr_is_waiting_input(void)
{
    /* 仅在 CONNECTING 状态且尚未发起 esp_wifi_connect()
       时（等待用户在串口输入密码）返回 true。
       自动重连路径中 g_connecting 已被设为 true，不在此列。 */
    return g_state == WIFI_MGR_CONNECTING && !g_connecting;
}

bool wifi_mgr_is_driver_on(void) { return g_wifi_enabled; }

bool wifi_mgr_is_connected(void)
{
    /* ── 获取调用方状态 ── */
    wifi_mgr_state_t cur_state = g_state;

    if (cur_state != WIFI_MGR_CONNECTED) {
        static wifi_mgr_state_t last_logged = WIFI_MGR_IDLE;
        if (cur_state != last_logged) {
            const char *state_names[] = {
                "IDLE", "WARMUP", "SCANNING", "SCAN_DONE",
                "CONNECTING", "CONNECTED", "CONNECT_FAILED"
            };
            const char *sname = (cur_state <= WIFI_MGR_CONNECT_FAILED)
                                ? state_names[cur_state] : "UNKNOWN";
            WIFI_LOGD("[is_connected] 状态=%d(%s) → false", cur_state, sname);
            last_logged = cur_state;
        }
        return false;
    }

    /* 二次确认：netif 确实持有有效 IP */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        WIFI_LOGD("[is_connected] state=CONNECTED 但 netif 为 NULL → false");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        WIFI_LOGD("[is_connected] state=CONNECTED 但 get_ip_info 失败 → false");
        return false;
    }

    if (ip_info.ip.addr == 0) {
        WIFI_LOGD("[is_connected] state=CONNECTED 但 IP=0.0.0.0 → false");
        return false;
    }

    /* ── 连接健全，打印 IP/DNS 信息 ── */
    char ip_str[16], gw_str[16], dns_str[16];
    ip_to_str(ip_info.ip.addr, ip_str, sizeof(ip_str));
    ip_to_str(ip_info.gw.addr, gw_str, sizeof(gw_str));

    esp_netif_dns_info_t dns_info;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
        ip_to_str(dns_info.ip.u_addr.ip4.addr, dns_str, sizeof(dns_str));
    } else {
        snprintf(dns_str, sizeof(dns_str), "N/A");
    }

    WIFI_LOGD("[is_connected] ✅ true | IP=%s GW=%s DNS=%s",
               ip_str, gw_str, dns_str);
    return true;
}

void wifi_mgr_ensure_dns(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        WIFI_LOGD("[ensure_dns] netif=NULL → 无法操作 DNS");
        return;
    }

    esp_netif_dns_info_t dns_info;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) != ESP_OK) {
        WIFI_LOGD("[ensure_dns] 获取 DNS 失败");
        return;
    }

    if (dns_info.ip.u_addr.ip4.addr != 0) {
        char cur_dns[16];
        ip_to_str(dns_info.ip.u_addr.ip4.addr, cur_dns, sizeof(cur_dns));
        WIFI_LOGD("[ensure_dns] DNS 已配置: %s (无需修改)", cur_dns);
        return;
    }

    WIFI_LOGD("[ensure_dns] ⚠️ DNS 为空! 设置为 8.8.8.8");
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    dns_info.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8");
    esp_err_t rc = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    if (rc != ESP_OK) {
        WIFI_LOGD("[ensure_dns] 设置 DNS 为 8.8.8.8 失败: %d", rc);
    } else {
        WIFI_LOGD("[ensure_dns] ✅ DNS 已设置为 8.8.8.8");
    }
}

uint32_t wifi_mgr_needed_heap(void) { return WIFI_MIN_FREE_HEAP; }

/* ═══ 初始化 ═══ */

void wifi_mgr_init(void)
{
    xeros_spinlock_init(&g_popup_spinlock);
    g_enable_requested  = false;
    g_disable_requested = false;

    /* ── 一次性初始化 ESP-IDF 网络子系统 ──
     * esp_netif、event loop、STA netif 在系统生命周期内只创建一次，
     * 不随 WiFi 开关销毁/重建。WiFi 驱动 (esp_wifi_init/deinit)
     * 在 enable/disable 中独立管理。 */
    esp_err_t rc = esp_netif_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed");
        return;
    }

    rc = esp_event_loop_create_default();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed");
        return;
    }

    /* 创建默认 STA netif（含 DHCP 客户端） */
    esp_netif_create_default_wifi_sta();

    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root) {
        for (int i = 0; i < root->child_num; i++) {
            if (root->child_list_item[i] &&
                root->child_list_item[i]->content &&
                strcmp(root->child_list_item[i]->content, "设置") == 0) {
                g_settings_list = root->child_list_item[i];
                break;
            }
        }
    }
}

/* ═══ 启用 / 禁用 ═══ */

void wifi_mgr_enable(void)
{
    kern_kmem_stat_t st;
    xeros_mem_get_stats(&st);
    ESP_LOGI(TAG, "enable start free=%lu max=%lu",
             (uint32_t)st.free_bytes,
             (uint32_t)st.largest_free_block);

    g_wifi_enabled = true;

    if (!xeros_mem_can_alloc(WIFI_MIN_FREE_HEAP, WIFI_MIN_MAX_ALLOC_HEAP)) {
        xeros_mem_get_stats(&st);
        ESP_LOGW(TAG, "heap guard failed: free=%lu max_alloc=%lu reserved=%lu",
                 (uint32_t)st.free_bytes,
                 (uint32_t)st.largest_free_block,
                 (uint32_t)kern_kmem_reserved_bytes());
        wifi_popup_request("内存不足", 2000);
        g_wifi_enabled = false;
        g_wifi_on = false;
        g_state = WIFI_MGR_IDLE;
        wifi_menu_rebuild_list(0);
        return;
    }

    /* ── 初始化 WiFi 驱动 ──
     * 注意：esp_netif / event loop / STA netif 已在 wifi_mgr_init() 中一次性创建，
     *       此处仅管理 WiFi 驱动 (esp_wifi_init/deinit) 的生命周期。 */
    if (!g_wifi_driver_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t rc = esp_wifi_init(&cfg);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_init failed");
            g_wifi_enabled = false;
            g_wifi_on = false;
            g_state = WIFI_MGR_IDLE;
            return;
        }
        g_wifi_driver_inited = true;
    }

    kern_sleep_ms(10); /* 让出 CPU，防止看门狗 */

    /* 注册 WiFi 和 IP 事件处理器（每次 enable 重新注册） */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               ip_event_handler, NULL);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_err_t rc = esp_wifi_set_mode(WIFI_MODE_STA);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed");
    }
    rc = esp_wifi_start();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed");
    }

    kern_sleep_ms(10); /* 让出 CPU */

    /* 重置事件标志 */
    g_scan_done = false;
    g_scan_ap_count = 0;
    g_evt_sta_connected = false;
    g_evt_sta_disconnected = false;
    g_evt_got_ip = false;
    g_evt_disconnect_reason = 0;
    g_reconnect_attempts = 0;

    g_warmup_start_time = hal_get_ticks();
    g_state = WIFI_MGR_WARMUP;
    g_auto_connect_done = false;
    g_is_auto_connect = false;
    wifi_menu_rebuild_list(0);
}

/* ═══ 扫描 SSID 访问器（供 wifi_menu.c 在 C 环境调用） ═══ */

extern "C" const char *wifi_mgr_get_scan_ssid(int index)
{
    static char buf[33];
    if (index < 0 || index >= (int)g_scan_ap_count || g_scan_ap_records == NULL) {
        buf[0] = '\0';
        return buf;
    }
    strncpy(buf, (const char *)g_scan_ap_records[index].ssid, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

/**
 * @brief 禁用 WiFi：断开连接、关闭驱动、清理菜单
 */
void wifi_mgr_disable(void)
{
    kern_kmem_stat_t st;
    xeros_mem_get_stats(&st);

    ESP_LOGI(TAG, "disable start free=%lu max=%lu",
             (uint32_t)st.free_bytes,
             (uint32_t)st.largest_free_block);

    wifi_infobar_dismiss();
    wifi_popup_dismiss();

    /* 注销事件处理器 */
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 ip_event_handler);

    esp_wifi_disconnect();
    kern_sleep_ms(50); /* 等待断开完成 */
    esp_wifi_stop();
    kern_sleep_ms(50); /* 等待停止完成 */
    esp_wifi_deinit();

    /* 注意：不销毁 STA netif / event loop / esp_netif，
     *       这些在系统生命周期内只创建一次（wifi_mgr_init）。 */

    g_wifi_enabled         = false;
    g_wifi_driver_inited   = false;

    xeros_mem_get_stats(&st);
    ESP_LOGI(TAG, "disable done free=%lu max=%lu",
             (uint32_t)st.free_bytes,
             (uint32_t)st.largest_free_block);

    if (g_networks_list) {
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
    g_reconnect_attempts  = 0;

    if (g_scan_ap_records) {
        free(g_scan_ap_records);
        g_scan_ap_records = NULL;
    }
    g_scan_ap_count = 0;
}

/* ═══ 异步请求接口（线程安全）═══ */

void wifi_mgr_request_enable(void)
{
    g_disable_requested = false;
    g_enable_requested  = true;
}

void wifi_mgr_request_disable(void)
{
    g_enable_requested  = false;
    g_disable_requested = true;
}

/**
 * @brief 在主任务上下文中统一处理 WiFi 启用/禁用请求
 * @note  WiFi 驱动操作（esp_wifi_scan_start 等）必须在该上下文中执行，
 *        避免跨任务调用导致 ESP-IDF 死锁或 TWDT 复位。
 */
void wifi_mgr_process_requests(void)
{
    if (g_disable_requested) {
        g_disable_requested = false;
        wifi_mgr_disable();
        return;
    }

    if (g_enable_requested) {
        g_enable_requested = false;
        wifi_mgr_enable();
    }
}

/* ═══ 开关切换回调 ═══ */

/**
 * @brief WiFi 开关切换回调（由 switch_item 的 exit_function 调用）
 */
void wifi_mgr_on_switch_toggle(void *ud)
{
    (void)ud;
    if (g_wifi_on) {
        wifi_mgr_request_enable();
    } else {
        wifi_mgr_request_disable();
    }
}

/* ═══ 回调函数 ═══ */

/**
 * @brief 可用网络按钮按下回调：请求串口输入密码并进入连接状态
 */
void wifi_menu_on_network_button_pressed(void *ud)
{
    (void)ud;
    if (!g_xerintosh_selector.selected_item) return;
    const char *content = g_xerintosh_selector.selected_item->content;
    if (!content) {
        return;
    }

    wifi_infobar_request("请在串口输入密码", 2000);
    serial_request_wifi_password(content);
    g_is_auto_connect = false;
    g_state = WIFI_MGR_CONNECTING;
}

/**
 * @brief 已保存网络"连接"按钮按下回调：读取密码并直接连接
 */
void wifi_menu_on_saved_connect_pressed(void *ud)
{
    (void)ud;
    if (!g_xerintosh_selector.selected_item || !g_xerintosh_selector.selected_item->parent) return;
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

    esp_wifi_disconnect();
    kern_sleep_ms(50);

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

    /* 重置事件标志 */
    g_evt_sta_connected = false;
    g_evt_sta_disconnected = false;
    g_evt_got_ip = false;
    g_evt_disconnect_reason = 0;

    esp_wifi_connect();

    strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
    strncpy(g_connecting_pass, pass,  STORAGE_PASS_MAX_LEN);
    g_connecting = true;
    g_connect_start_time = hal_get_ticks();
    g_is_auto_connect = false;
    g_state = WIFI_MGR_CONNECTING;
    wifi_infobar_request("连接中...", 2000);

    xerintosh_selector_exit_current_item();
}

/**
 * @brief 已保存网络"删除"按钮按下回调
 */
void wifi_menu_on_saved_delete_pressed(void *ud)
{
    (void)ud;
    if (!g_xerintosh_selector.selected_item || !g_xerintosh_selector.selected_item->parent) return;
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
    wifi_menu_rebuild_list(0);
}

/**
 * @brief 扫描按钮按下回调：启动异步网络扫描
 */
void wifi_menu_on_scan_pressed(void *ud)
{
    (void)ud;
    if (g_connecting) {
        esp_wifi_disconnect();
        kern_sleep_ms(50);
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
        g_wifi_scan_start_time = hal_get_ticks();
        wifi_infobar_request("扫描中...", 2000);
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

    if (g_scan_ap_count == 0) return false;

    int best_saved_idx = -1;
    int8_t best_rssi = -128;

    for (int i = 0; i < saved_count; i++) {
        char ssid[STORAGE_SSID_MAX_LEN];
        char pass[STORAGE_PASS_MAX_LEN];
        if (!storage_wifi_get(i, ssid, pass)) continue;

        for (int j = 0; j < (int)g_scan_ap_count; j++) {
            if (g_scan_ap_records[j].ssid[0] == '\0') continue;
            if (strcmp((const char *)g_scan_ap_records[j].ssid, ssid) == 0) {
                int8_t rssi = g_scan_ap_records[j].rssi;
                if (rssi > best_rssi) {
                    best_rssi = rssi;
                    best_saved_idx = i;
                }
                break;
            }
        }
    }

    if (best_saved_idx < 0) {
        WIFI_LOGD("[auto_connect] 扫描结果中没有匹配的已保存网络");
        return false;
    }

    char ssid[STORAGE_SSID_MAX_LEN];
    char pass[STORAGE_PASS_MAX_LEN];
    if (!storage_wifi_get(best_saved_idx, ssid, pass)) return false;

    WIFI_LOGD("[auto_connect] 自动连接 SSID='%s' (RSSI=%d)", ssid, best_rssi);

    esp_wifi_disconnect();
    kern_sleep_ms(50);

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

    /* 重置事件标志 */
    g_evt_sta_connected = false;
    g_evt_sta_disconnected = false;
    g_evt_got_ip = false;

    esp_wifi_connect();

    strncpy(g_connecting_ssid, ssid, STORAGE_SSID_MAX_LEN);
    strncpy(g_connecting_pass, pass,  STORAGE_PASS_MAX_LEN);
    g_connecting = true;
    g_connect_start_time = hal_get_ticks();
    g_is_auto_connect = true;
    g_state = WIFI_MGR_CONNECTING;
    return true;
}

/* ═══ 每帧更新（事件驱动状态机）═══ */

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
        if (hal_get_ticks() - g_warmup_start_time >= WIFI_WARMUP_DELAY_MS) {
            static uint8_t warmup_scan_retries = 0;
            g_scan_done = false;
            g_scan_ap_count = 0;
            wifi_scan_config_t scan_cfg = {};
            scan_cfg.show_hidden = true;
            esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
            if (err != ESP_OK) {
                if (warmup_scan_retries < 3) {
                    warmup_scan_retries++;
                    g_warmup_start_time = hal_get_ticks();  /* 重试 */
                } else {
                    warmup_scan_retries = 0;
                    if (!g_initial_scan_shown) {
                        wifi_popup_request("扫描失败", 2000);
                    }
                    g_state = WIFI_MGR_SCAN_DONE;
                    g_initial_scan_shown = true;
                }
            } else {
                warmup_scan_retries = 0;
                g_state = WIFI_MGR_SCANNING;
                g_wifi_scan_start_time = hal_get_ticks();
                if (!g_initial_scan_shown) {
                    wifi_infobar_request("扫描中...", 2000);
                }
            }
        }
        break;
    }

    case WIFI_MGR_SCANNING: {
        if (hal_get_ticks() - g_wifi_scan_start_time >= WIFI_SCAN_TIMEOUT_MS) {
            if (!g_initial_scan_shown) {
                wifi_infobar_dismiss();
                wifi_popup_dismiss();
                WIFI_LOGD("[STATE] SCANNING→SCAN_DONE (30s 超时)");
            }
            wifi_menu_rebuild_list(0);
            g_state = WIFI_MGR_SCAN_DONE;
            g_initial_scan_shown = true;
            break;
        }

        if (g_scan_done) {
            uint16_t ap_count = WIFI_SCAN_MAX_AP;
            if (g_scan_ap_records == NULL) {
                g_scan_ap_records = (wifi_ap_record_t *)malloc(
                    sizeof(wifi_ap_record_t) * WIFI_SCAN_MAX_AP);
            }
            if (g_scan_ap_records) {
                esp_wifi_scan_get_ap_records(&ap_count, g_scan_ap_records);
                g_scan_ap_count = ap_count;
                WIFI_LOGD("[STATE] SCANNING→SCAN_DONE (发现 %d 个 AP)", ap_count);
            } else {
                g_scan_ap_count = 0;
                wifi_popup_request("内存不足，无法扫描", 2000);
                WIFI_LOGD("[STATE] SCANNING→SCAN_DONE (malloc AP 记录失败)");
            }

            if (!g_initial_scan_shown) {
                wifi_infobar_dismiss();
                wifi_popup_dismiss();
            }
            wifi_menu_rebuild_list(g_scan_ap_count);
            if (g_scan_ap_count > 0 && !g_initial_scan_shown) {
                wifi_infobar_request("扫描完毕", 1500);
            }
            g_state = WIFI_MGR_SCAN_DONE;
            g_initial_scan_shown = true;
        }
        break;
    }

    case WIFI_MGR_CONNECTING: {
        /* ── 第一步：轮询串口密码输入 ── */
        serial_state_t ss = serial_poll();
        if (ss == SERIAL_STATE_PASSWORD_RECEIVED) {
            const char *input  = serial_get_input();
            const char *target = serial_get_target_name();
            if (input && target) {
                WIFI_LOGD("[STATE] 收到串口密码, 连接 SSID='%s'", target);
                esp_wifi_disconnect();
                kern_sleep_ms(50);

                wifi_config_t wifi_cfg = {};
                strncpy((char *)wifi_cfg.sta.ssid, target, sizeof(wifi_cfg.sta.ssid) - 1);
                strncpy((char *)wifi_cfg.sta.password, input, sizeof(wifi_cfg.sta.password) - 1);
                esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

                /* 重置事件标志 */
                g_evt_sta_connected = false;
                g_evt_sta_disconnected = false;
                g_evt_got_ip = false;

                esp_wifi_connect();

                strncpy(g_connecting_ssid, target, STORAGE_SSID_MAX_LEN);
                strncpy(g_connecting_pass, input,  STORAGE_PASS_MAX_LEN);
                g_connecting = true;
                g_connect_start_time = hal_get_ticks();
                wifi_infobar_request("连接中...", 2000);
            }
        } else if (ss == SERIAL_STATE_CANCELLED) {
            WIFI_LOGD("[STATE] CONNECTING→SCAN_DONE (密码输入取消)");
            g_connecting = false;
            g_state = WIFI_MGR_SCAN_DONE;
            wifi_infobar_dismiss();
            wifi_popup_dismiss();
            break;
        }

        /* ── 第二步：事件驱动连接状态检测 ── */
        if (!g_connecting) break; /* 仍在等待密码输入 */

        /* 检查超时 */
        if (hal_get_ticks() - g_connect_start_time >= WIFI_CONNECT_TIMEOUT_MS) {
            esp_wifi_disconnect();
            kern_sleep_ms(50);
            g_connecting = false;
            WIFI_LOGD("[STATE] CONNECTING→CONNECT_FAILED (15s 超时, SSID='%s')",
                       g_connecting_ssid);
            if (!g_is_auto_connect) {
                wifi_popup_request("连接超时", 2000);
            }
            /* 连接失败也释放扫描记录回收内存 */
            if (g_scan_ap_records) { free(g_scan_ap_records); g_scan_ap_records = NULL; g_scan_ap_count = 0; }
            g_state = WIFI_MGR_CONNECT_FAILED;
            break;
        }

        /* 事件：已断开（密码错误或 AP 拒绝） */
        if (g_evt_sta_disconnected) {
            uint8_t reason = g_evt_disconnect_reason;
            g_evt_sta_disconnected = false;
            g_connecting = false;
            WIFI_LOGD("[STATE] CONNECTING→CONNECT_FAILED (断开 reason=%d, SSID='%s')",
                       reason, g_connecting_ssid);
            if (!g_is_auto_connect) {
                char reason_buf[48];
                snprintf(reason_buf, sizeof(reason_buf),
                         "连接失败 (%d)", reason);
                wifi_popup_request(reason_buf, 3000);
            }
            g_state = WIFI_MGR_CONNECT_FAILED;
            break;
        }

        /* 事件：L2 已连接 + DHCP 已获取 IP → 真正的 L3 连接成功 */
        if (g_evt_sta_connected && g_evt_got_ip) {
            g_evt_sta_connected = false;
            g_evt_got_ip = false;
            g_connecting = false;
            storage_wifi_add(g_connecting_ssid, g_connecting_pass);
            WIFI_LOGD("[STATE] ✅ CONNECTING→CONNECTED (SSID='%s')",
                       g_connecting_ssid);
            if (!g_is_auto_connect) {
                wifi_popup_request("已连接", 1500);
            }

            /* 连接成功，释放扫描结果以回收内存 */
            if (g_scan_ap_records) {
                free(g_scan_ap_records);
                g_scan_ap_records = NULL;
                g_scan_ap_count = 0;
                WIFI_LOGD("[STATE] 已释放扫描 AP 记录 (回收 ~2.5KB)");
            }

            g_state = WIFI_MGR_CONNECTED;
            wifi_menu_rebuild_list(0);
        }
        break;
    }

    case WIFI_MGR_SCAN_DONE: {
        if (!g_auto_connect_done) {
            g_auto_connect_done = true;
            WIFI_LOGD("[STATE] SCAN_DONE → 尝试自动连接...");
            bool connected = try_auto_connect();
            WIFI_LOGD("[STATE]   自动连接 %s", connected ? "已发起" : "无可用已保存网络");
        }
        break;
    }

    case WIFI_MGR_CONNECTED: {
        /* ═══ 被动断开检测（事件驱动）═══
         * 仅在已连接情况下检测断开事件。
         * 断开由 WiFi 硬件事件触发（AP 掉线、信号丢失等），
         * 无需轮询 esp_wifi_sta_get_ap_info()。 */
        if (g_evt_sta_disconnected) {
            g_evt_sta_disconnected = false;
            uint8_t reason = g_evt_disconnect_reason;

            /* 判断是否需要自动重连：仅对短暂性原因重连 */
            bool should_retry = false;
            switch (reason) {
            case WIFI_REASON_BEACON_TIMEOUT:    /* 200: AP 信标丢失 */
            case WIFI_REASON_NO_AP_FOUND:       /* 201: 扫描中未找到 AP */
            case WIFI_REASON_HANDSHAKE_TIMEOUT: /* 204: WPA 握手超时 */
            case WIFI_REASON_CONNECTION_FAIL:   /* 205: 通用连接失败 */
                should_retry = true;
                break;
            case WIFI_REASON_AUTH_FAIL:         /* 202: 认证失败——密码错误 */
            case WIFI_REASON_ASSOC_FAIL:        /* 203: 关联被拒 */
            case WIFI_REASON_AUTH_EXPIRE:       /* 2: 认证过期 */
            default:
                break; /* 永久性失败，不重试 */
            }

            if (should_retry && g_reconnect_attempts < WIFI_MAX_RECONNECT_ATTEMPTS) {
                g_reconnect_attempts++;
                WIFI_LOGD("[STATE] CONNECTED→CONNECTING (重连 %d/3, reason=%d)",
                           g_reconnect_attempts, reason);

                char retry_buf[64];
                snprintf(retry_buf, sizeof(retry_buf),
                         "重连中 (%d/%d)...",
                         g_reconnect_attempts, WIFI_MAX_RECONNECT_ATTEMPTS);
                wifi_infobar_request(retry_buf, 2000);

                /* 重置事件标志后重新连接 */
                g_evt_sta_connected = false;
                g_evt_got_ip = false;
                g_evt_disconnect_reason = 0;

                esp_wifi_disconnect();
                kern_sleep_ms(100);
                esp_wifi_connect();

                g_connecting = true;
                g_connect_start_time = hal_get_ticks();
                g_state = WIFI_MGR_CONNECTING;
            } else {
                /* 放弃重连 */
                if (g_reconnect_attempts > 0) {
                    wifi_popup_request("重连失败", 2000);
                    WIFI_LOGD("[STATE] CONNECTED→CONNECT_FAILED (放弃重连, reason=%d)", reason);
                } else {
                    WIFI_LOGD("[STATE] CONNECTED→CONNECT_FAILED (reason=%d, 不可重连原因)", reason);
                }
                g_reconnect_attempts = 0;
                g_connecting = false;
                g_state = WIFI_MGR_CONNECT_FAILED;
            }
        }
        break;
    }

    case WIFI_MGR_CONNECT_FAILED:
        /* 等待用户操作（重新扫描、选择其他网络等） */
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
