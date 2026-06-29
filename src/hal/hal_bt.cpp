/**
 * @file   hal_bt.cpp
 * @brief  Bluetooth 硬件抽象层实现 (Classic BT SPP)
 * @details ESP-IDF 原生 BT API 封装。
 *
 *          SPP 回调在 Bluedroid 内部 FreeRTOS 任务上下文中执行，
 *          不能安全调用 Xeros 内核函数。回调中将数据推入原子保护的
 *          环形缓冲区，由 hal_bt_poll() 在主循环中消费。
 *
 *          NATIVE_TEST 时所有函数为空操作桩。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_bt.h"

#if !defined(NATIVE_TEST) && defined(CONFIG_BT_ENABLED)

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_spp_api.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"

#include <string.h>

/* ═══ 环形缓冲区 ═══ */

#define HAL_BT_RX_BUF_SIZE  1024

static uint8_t  s_bt_rx_buf[HAL_BT_RX_BUF_SIZE];
static volatile uint16_t s_bt_rx_head  = 0;
static volatile uint16_t s_bt_rx_tail  = 0;
static volatile uint16_t s_bt_rx_count = 0;

static volatile bool s_bt_spinlock = false;

static inline void hal_bt_spin_lock(void)
{
    while (__sync_lock_test_and_set(&s_bt_spinlock, true)) {
        /* 忙等：抢占式调度器会自动切换回锁持有者任务 */
    }
}

static inline void hal_bt_spin_unlock(void)
{
    __sync_lock_release(&s_bt_spinlock);
}

/* ═══ 内部状态 ═══ */

static bool s_bt_initialized = false;
static volatile uint32_t s_spp_handle = 0;
static volatile bool s_spp_connected = false;

/* ═══ GAP 回调（用于调试/事件追踪） ═══ */

static void hal_bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI("hal_bt", "GAP mode changed: mode=%d", param->mode_chg.mode);
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI("hal_bt", "GAP auth completed, device=%s",
                     (const char *)(param->auth_cmpl.device_name ? param->auth_cmpl.device_name : (uint8_t *)""));
        } else {
            ESP_LOGI("hal_bt", "GAP auth failed: %d", param->auth_cmpl.stat);
        }
        break;
    default:
        break;
    }
}

/* ═══ SPP 回调 ═══ */

static void hal_bt_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_SRV_OPEN_EVT:
        if (param->srv_open.status == ESP_SPP_SUCCESS) {
            s_spp_handle    = param->srv_open.handle;
            s_spp_connected = true;
            ESP_LOGI("hal_bt", "SPP client connected, handle=%lu", (unsigned long)s_spp_handle);
        }
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI("hal_bt", "SPP disconnected, handle=%lu status=%lu",
                 (unsigned long)param->close.handle,
                 (unsigned long)param->close.port_status);
        s_spp_connected = false;
        s_spp_handle    = 0;
        break;

    case ESP_SPP_DATA_IND_EVT:
        /*
         * SPP 回调默认不拿到 conn_handle (K33)。
         * esp_spp_write 依赖正确的 handle 来 identify 连接。
         * 数据到达时应确保 handle 已通过 SRV_OPEN_EVT 记录。
         */
        if (param->data_ind.data != NULL && param->data_ind.len > 0) {
            ESP_LOGI("hal_bt", "SPP rx: len=%d", param->data_ind.len);
            hal_bt_spin_lock();
            uint16_t len = param->data_ind.len;
            for (uint16_t i = 0; i < len; i++) {
                uint16_t next_head = (s_bt_rx_head + 1) % HAL_BT_RX_BUF_SIZE;
                if (next_head == s_bt_rx_tail) {
                    /* 缓冲区满，丢弃最旧字节 */
                    s_bt_rx_tail = (s_bt_rx_tail + 1) % HAL_BT_RX_BUF_SIZE;
                } else {
                    s_bt_rx_count++;
                }
                s_bt_rx_buf[s_bt_rx_head] = param->data_ind.data[i];
                s_bt_rx_head = next_head;
            }
            hal_bt_spin_unlock();
        }
        break;

    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI("hal_bt", "SPP server started, scn=%d", param->start.scn);
            /* SPP 服务器就绪后再设可发现/可连接模式 */
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        } else {
            ESP_LOGE("hal_bt", "SPP server start failed");
        }
        break;

    case ESP_SPP_WRITE_EVT:
        /* 写操作完成，忽略 */
        break;

    case ESP_SPP_CONG_EVT:
        /* 拥塞状态变化，忽略 */
        break;

    default:
        break;
    }
}

/* ═══ 公共 API ═══ */

bool hal_bt_init(void)
{
    if (s_bt_initialized) return true;

    /*
     * BT/WiFi 共存策略：两者不可同时启用。
     * 调用者 (bt_uart_service) 在调用 hal_bt_init 前会确保 WiFi 已关闭。
     */

    /* 先释放可能残留的 BT 状态 */
    hal_bt_deinit();

    /* 1. 初始化 BT Controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("hal_bt", "BT controller init failed: %d", err);
        return false;
    }

    /* 2. 启用 BT Controller（Classic BT 模式） */
    err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        ESP_LOGE("hal_bt", "BT controller enable failed: %d", err);
        esp_bt_controller_deinit();
        return false;
    }

    /* 3. 初始化 Bluedroid 栈 */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_cfg.ssp_en = true;   /* 启用 Secure Simple Pairing */
    bluedroid_cfg.sc_en  = false;  /* 不强制 Secure Connections */

    err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("hal_bt", "Bluedroid init failed: %d", err);
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    /* 4. 启用 Bluedroid */
    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE("hal_bt", "Bluedroid enable failed: %d", err);
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    /* 5. 设置设备名+注册 GAP 回调（必须在 Bluedroid 启用后） */
    esp_bt_gap_set_device_name("M5Stick-P1");
    esp_bt_gap_register_callback(hal_bt_gap_callback);

    /* 6. 注册 SPP 回调 + 初始化 SPP */
    esp_spp_register_callback(hal_bt_spp_callback);

    esp_spp_cfg_t spp_cfg = {
        .mode             = ESP_SPP_MODE_CB,   /* 回调模式，数据通过回调到达 */
        .enable_l2cap_ertm = false,             /* 关闭 ERTM 提升兼容性 */
        .tx_buffer_size   = 0,                  /* CB 模式下忽略 */
    };
    err = esp_spp_enhanced_init(&spp_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("hal_bt", "SPP init failed: %d", err);
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    /* 7. 启动 SPP 服务器（显式创建 SDP 记录确保客户端可发现服务） */
    esp_spp_start_srv_cfg_t srv_cfg = {
        .local_scn        = 0,               /* 自动分配信道 */
        .create_spp_record = true,            /* 显式创建 SPP SDP 记录 */
        .sec_mask         = ESP_SPP_SEC_NONE, /* 无需 PIN 码/配对 */
        .role             = ESP_SPP_ROLE_SLAVE,
        .name             = "M5Stick-P1",
    };
    err = esp_spp_start_srv_with_cfg(&srv_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("hal_bt", "SPP start srv failed: %d", err);
        esp_spp_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    s_bt_initialized = true;
    ESP_LOGI("hal_bt", "BT init complete, ready for SPP connections");
    return true;
}

void hal_bt_deinit(void)
{
    /*
     * 完整释放顺序：SPP → Bluedroid → BT Controller
     * 确保任何残留栈状态被彻底清理，下次 init 从零开始。
     */
    if (s_bt_initialized) {
        s_spp_connected = false;
        s_spp_handle    = 0;
        s_bt_initialized = false;

        esp_spp_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();

        ESP_LOGI("hal_bt", "BT fully deinitialized");
    }

    /* 清空 RX 缓冲区 */
    hal_bt_spin_lock();
    s_bt_rx_head  = 0;
    s_bt_rx_tail  = 0;
    s_bt_rx_count = 0;
    hal_bt_spin_unlock();
}

int hal_bt_spp_read(uint8_t *buf, int len)
{
    if (buf == NULL || len <= 0) return 0;

    hal_bt_spin_lock();
    int count = 0;
    while (count < len && s_bt_rx_count > 0) {
        buf[count++] = s_bt_rx_buf[s_bt_rx_tail];
        s_bt_rx_tail = (uint16_t)((s_bt_rx_tail + 1) % HAL_BT_RX_BUF_SIZE);
        s_bt_rx_count--;
    }
    hal_bt_spin_unlock();
    return count;
}

int hal_bt_spp_write(const uint8_t *data, int len)
{
    if (data == NULL || len <= 0) return 0;
    if (!s_spp_connected || s_spp_handle == 0) return 0;

    esp_err_t err = esp_spp_write(s_spp_handle, len, (uint8_t *)data);
    if (err != ESP_OK) return 0;
    return len;
}

int hal_bt_spp_available(void)
{
    return (int)s_bt_rx_count;
}

bool hal_bt_spp_is_connected(void)
{
    return s_spp_connected;
}

void hal_bt_poll(void)
{
    /*
     * SPP 数据已经通过回调写入环形缓冲区。
     * hal_bt_poll() 本身不需要做额外工作——数据已经就绪，
     * 等待 hal_bt_spp_read() 消费。
     *
     * 空轮询保留用于未来扩展（如连接状态轮询、心跳检测等）。
     */
}

#elif defined(NATIVE_TEST)
/* ═══ NATIVE_TEST 桩实现 ═══ */

bool hal_bt_init(void) { return true; }
void hal_bt_deinit(void) {}

int hal_bt_spp_read(uint8_t *buf, int len) { (void)buf; (void)len; return 0; }
int hal_bt_spp_write(const uint8_t *data, int len) { (void)data; (void)len; return len; }
int hal_bt_spp_available(void) { return 0; }
bool hal_bt_spp_is_connected(void) { return false; }
void hal_bt_poll(void) {}

#else
/* ═══ CONFIG_BT_ENABLED 未定义 — 空桩 ═══ */

bool hal_bt_init(void) { return false; }
void hal_bt_deinit(void) {}

int hal_bt_spp_read(uint8_t *buf, int len) { (void)buf; (void)len; return 0; }
int hal_bt_spp_write(const uint8_t *data, int len) { (void)data; (void)len; return len; }
int hal_bt_spp_available(void) { return 0; }
bool hal_bt_spp_is_connected(void) { return false; }
void hal_bt_poll(void) {}

#endif
