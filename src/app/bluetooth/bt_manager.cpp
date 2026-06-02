/**
 * @file   bt_manager.cpp
 * @brief  蓝牙管理器实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：基于 NimBLE 的完整状态机实现，
 *            支持 BLE 设备扫描、串口配对码输入、已保存设备管理及 UI 菜单动态构建。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include "app/bluetooth/bt_manager.h"

void bt_mgr_init(void) {}
void bt_mgr_enable(void) {}
void bt_mgr_disable(void) {}
bool bt_mgr_is_waiting_input(void) { return false; }
void bt_mgr_update(void) {}
void bt_mgr_on_switch_toggle(void *ud) { (void)ud; }

#else

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app/bluetooth/bt_manager.h"

extern "C" {
#include "app/storage/storage.h"
#include "app/serial_input/serial_input.h"
#include "app/svc_mgr_helper.h"
#include "app/ui_service.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"
#include "kernel/kern_task.h"
}

extern bool g_bt_on;  /* 定义在 main.cpp */

/* ═══ 内部状态 ═══ */

static bool g_bt_enabled = false;           /* 蓝牙是否已启用 */
static bt_mgr_state_t g_state = BT_MGR_IDLE; /* 状态机当前状态 */

/* BLE 扫描结果 */
struct BleDeviceResult {
    char name[STORAGE_BT_NAME_MAX_LEN];
    char address[STORAGE_BT_ADDR_MAX_LEN];
    int rssi;
};

static BleDeviceResult g_scan_results[20];  /* 扫描结果缓冲区 */
static int g_scan_result_count = 0;         /* 当前扫描结果数量 */
static unsigned long g_scan_start_time = 0; /* 扫描开始时间 */
static unsigned long g_warmup_start_time = 0; /* 预热开始时间 */
static const unsigned long SCAN_DURATION_MS = 10000; /* 扫描持续时间 */
#define BT_WARMUP_DELAY_MS 1500             /* 预热等待时间 */

/* 异步扫描任务 */
static volatile bool g_scan_task_running = false;  /* 扫描任务是否正在运行 */
static TaskHandle_t g_scan_task_handle = NULL;     /* 扫描任务句柄 */

/* UI 菜单指针 */
static xerintosh_list_item_t *g_settings_list = NULL;  /* "设置" 列表项 */
static xerintosh_list_item_t *g_devices_list = NULL;   /* "蓝牙设备" 列表项 */

/* ═══ 前向声明 ═══ */

static void rebuild_device_list(void);
static void on_device_button_pressed(void *ud);
static void on_bt_reconnect_pressed(void *ud);
static void on_bt_delete_pressed(void *ud);
static void on_bt_scan_pressed(void *ud);
extern "C" void bt_mgr_task_main(void *arg);

/* ═══ 异步扫描 FreeRTOS 任务 ═══ */

/**
 * @brief BLE 扫描 FreeRTOS 任务入口
 * @note  在独立任务中执行 NimBLE 阻塞式扫描，避免阻塞 UI 任务。
 *        扫描完成后自动删除自身。
 */
static void bt_scan_task(void *arg) {
    (void)arg;
    NimBLEDevice::getScan()->start(SCAN_DURATION_MS / 1000, false);
    g_scan_task_running = false;
    g_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ═══ BLE 扫描回调（NimBLE API）═══ */

/**
 * @brief NimBLE 广告设备发现回调
 * @note  每发现一个设备即被调用，结果存入 g_scan_results
 */
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (g_scan_result_count >= 20) return;

        BleDeviceResult &r = g_scan_results[g_scan_result_count];
        NimBLEAddress bleAddr = advertisedDevice->getAddress();
        std::string addr = bleAddr.toString();
        strlcpy(r.address, addr.c_str(), STORAGE_BT_ADDR_MAX_LEN);

        /* 尝试获取设备名称 */
        std::string name = advertisedDevice->getName();
        if (name.empty()) {
            /* 无名设备：用 MAC 后 3 字节生成短标识，如 "BLE_d4e5f6" */
            const uint8_t *raw = bleAddr.getNative();
            char short_name[16];
            snprintf(short_name, sizeof(short_name), "BLE_%02x%02x%02x",
                     raw[3], raw[4], raw[5]);
            name = short_name;
        }
        strlcpy(r.name, name.c_str(), STORAGE_BT_NAME_MAX_LEN);

        r.rssi = advertisedDevice->getRSSI();
        g_scan_result_count++;
    }
};

/* ═══ 菜单回调 ═══ */

/**
 * @brief 扫描到的设备按钮按下回调：请求串口输入配对码
 */
static void on_device_button_pressed(void *ud) {
    (void)ud;
    xerintosh_list_item_t *item = g_xerintosh_selector.selected_item;
    if (!item || !item->content) return;
    ui_svc_popup("请在串口输入配对码", 100);
    const char *addr = (const char*)item->user_data;
    if (addr && addr[0]) {
        serial_request_bt_pair_code_with_addr(item->content, addr);
    } else {
        serial_request_bt_pair_code(item->content);
    }
    g_state = BT_MGR_PAIRING;
}

/**
 * @brief 已保存设备"重新连接"按钮按下回调：异步重新扫描该设备
 */
static void on_bt_reconnect_pressed(void *ud) {
    (void)ud;
    xerintosh_list_item_t *parent = g_xerintosh_selector.selected_item->parent;
    if (!parent || !parent->user_data) return;

    char addr[STORAGE_BT_ADDR_MAX_LEN];
    strlcpy(addr, (const char*)parent->user_data, sizeof(addr));

    ui_svc_notify_loading("搜索中...");
    xerintosh_selector_exit_current_item();

    g_scan_result_count = 0;
    g_state = BT_MGR_STARTING_SCAN;
}

/**
 * @brief 已保存设备"删除"按钮按下回调
 */
static void on_bt_delete_pressed(void *ud) {
    (void)ud;
    xerintosh_list_item_t *parent = g_xerintosh_selector.selected_item->parent;
    if (!parent || !parent->user_data) return;

    char addr[STORAGE_BT_ADDR_MAX_LEN];
    strlcpy(addr, (const char*)parent->user_data, sizeof(addr));

    int idx = storage_bt_find(addr);
    if (idx >= 0) storage_bt_remove(idx);

    ui_svc_notify_success("已删除");
    xerintosh_selector_exit_current_item();
    rebuild_device_list();
}

/**
 * @brief 扫描按钮按下回调：启动异步 BLE 设备扫描
 * @note  扫描在独立 FreeRTOS 任务中执行，避免阻塞 UI
 */
static void on_bt_scan_pressed(void *ud) {
    (void)ud;
    g_scan_result_count = 0;
    g_state = BT_MGR_STARTING_SCAN;
    ui_svc_popup("扫描中...", SCAN_DURATION_MS);
}

/* ═══ 设备列表重建 ═══ */

/**
 * @brief 重建蓝牙设备子菜单（扫描结果 + 扫描按钮）
 * @note  根据 g_scan_results 动态构建菜单，已保存设备标记 * 号
 */
static void rebuild_device_list(void) {
    Serial.printf("[BT] rebuild_device_list: g_devices_list=%p, scan_count=%d\n",
                  g_devices_list, g_scan_result_count);

    /* 首次调用时创建设备列表项 */
    if (!g_devices_list) {
        g_devices_list = xerintosh_new_list_item("蓝牙设备", list_icon);
        Serial.printf("[BT] Created g_devices_list=%p\n", g_devices_list);
        if (g_settings_list && g_devices_list) {
            xerintosh_push_item_to_list(g_settings_list, g_devices_list);
            Serial.printf("[BT] Pushed to settings list\n");
        }
    }

    if (!g_devices_list) return;

    /* 安全处理：若选择器位于设备子树内，先将其移到设备项本身 */
    ui_selector_rebuild_anchor(g_devices_list, g_settings_list);

    xerintosh_clear_children_of_list(g_devices_list);
    Serial.printf("[BT] Cleared children, now adding %d scan results\n", g_scan_result_count);

    /* 最多显示 9 个扫描结果 */
    for (int i = 0; i < g_scan_result_count && i < 9; i++) {
        int8_t saved_idx = storage_bt_find(g_scan_results[i].address);

        /* 已保存设备名称后加 * 标记 */
        char display_name[STORAGE_BT_NAME_MAX_LEN + 2];
        if (saved_idx >= 0) {
            snprintf(display_name, sizeof(display_name), "%s*",
                     g_scan_results[i].name);
        } else {
            snprintf(display_name, sizeof(display_name), "%s",
                     g_scan_results[i].name);
        }

        xerintosh_list_item_t *item;
        if (saved_idx >= 0) {
            /* 已保存设备：显示子菜单（重新连接 / 删除） */
            item = xerintosh_new_list_item(display_name, list_icon);
            xerintosh_list_item_t *reconnect =
                xerintosh_new_button_item("重新连接", on_bt_reconnect_pressed,
                                      default_icon);
            xerintosh_list_item_t *del =
                xerintosh_new_button_item("删除", on_bt_delete_pressed,
                                      default_icon);
            xerintosh_push_item_to_list(item, reconnect);
            xerintosh_push_item_to_list(item, del);
        } else {
            /* 未保存设备：直接作为配对按钮 */
            item = xerintosh_new_button_item(display_name,
                                         on_device_button_pressed, default_icon);
        }

        /* 将 MAC 地址存入 user_data 供后续使用 */
        item->user_data = strdup(g_scan_results[i].address);
        xerintosh_push_item_to_list(g_devices_list, item);
    }

    xerintosh_list_item_t *scan_btn =
        xerintosh_new_button_item("扫描", on_bt_scan_pressed, default_icon);
    xerintosh_push_item_to_list(g_devices_list, scan_btn);

    Serial.printf("[BT] rebuild done: child_num=%d\n", g_devices_list->child_num);

    /* 重建后，若选择器位于设备项上，移至其第一个子项 */
    if (g_devices_list && g_devices_list->child_num > 0) {
        ui_selector_rebuild_anchor(g_devices_list, g_settings_list);
    }
}

/* ═══ 公共 API ═══ */

/**
 * @brief 初始化蓝牙管理器
 */
void bt_mgr_init(void) {
    g_bt_enabled = false;
    g_state = BT_MGR_IDLE;
    g_devices_list = NULL;
    g_scan_result_count = 0;

    xerintosh_list_item_t *root = xerintosh_get_root_list();
    if (root && root->child_num > 0) {
        g_settings_list = root->child_list_item[0];  /* "设置" */
    }
}

/**
 * @brief 启用蓝牙：初始化 NimBLE，配置扫描参数，开始预热
 */
void bt_mgr_enable(void) {
    Serial.println("[BT] bt_mgr_enable called");
    g_bt_enabled = true;
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
    scan->setActiveScan(true);

    g_scan_result_count = 0;
    g_warmup_start_time = millis();
    g_state = BT_MGR_WARMUP;
    Serial.println("[BT] Calling rebuild_device_list");
    rebuild_device_list();
    Serial.println("[BT] bt_mgr_enable done");
}

/**
 * @brief 禁用蓝牙：释放 NimBLE，清理菜单
 */
void bt_mgr_disable(void) {
    NimBLEDevice::deinit(true);
    g_bt_enabled = false;

    if (g_devices_list) {
        ui_selector_safety_move_out(g_devices_list, g_settings_list);

        xerintosh_clear_children_of_list(g_devices_list);
        if (g_settings_list) {
            xerintosh_remove_item_from_list(g_settings_list, g_devices_list);
        }
        g_devices_list = NULL;
    }

    g_state = BT_MGR_IDLE;
}

bool bt_mgr_is_waiting_input(void) {
    return g_state == BT_MGR_PAIRING;
}

/**
 * @brief 每帧更新蓝牙状态机（非阻塞）
 */
void bt_mgr_update(void) {
    if (!g_bt_enabled && g_state == BT_MGR_IDLE) return;

    static bt_mgr_state_t last_state = BT_MGR_IDLE;
    if (g_state != last_state) {
        Serial.printf("[BT] State: %d -> %d\n", last_state, g_state);
        last_state = g_state;
    }

    switch (g_state) {
    case BT_MGR_WARMUP: {
        /* 预热完成后自动启动首次扫描（后台，不阻塞 UI） */
        if (millis() - g_warmup_start_time >= BT_WARMUP_DELAY_MS) {
            g_state = BT_MGR_STARTING_SCAN;
        }
        break;
    }
    case BT_MGR_STARTING_SCAN: {
        /* 在独立 FreeRTOS 任务中启动 NimBLE 扫描（阻塞式），
         * 避免阻塞 UI 任务导致看门狗重启 */
        if (!g_scan_task_running) {
            g_scan_task_running = true;
            BaseType_t ret = xTaskCreate(
                bt_scan_task, "bt_scan", 4096, NULL, 1, &g_scan_task_handle);
            if (ret != pdPASS) {
                g_scan_task_running = false;
                g_state = BT_MGR_SCAN_DONE;
                ui_svc_popup_hide();
                ui_svc_notify_error("扫描失败");
                break;
            }
            g_scan_start_time = millis();
            g_state = BT_MGR_SCANNING;
        }
        break;
    }
    case BT_MGR_SCANNING: {
        /* 扫描任务完成（回调已设置 g_scan_task_running=false）或超时 */
        bool task_done = !g_scan_task_running;
        bool timed_out = (millis() - g_scan_start_time >= SCAN_DURATION_MS + 2000);
        if (task_done || timed_out) {
            if (!task_done && g_scan_task_handle) {
                /* 超时：强制清理任务句柄（任务可能已自行退出） */
                g_scan_task_running = false;
                g_scan_task_handle = NULL;
            }
            NimBLEDevice::getScan()->stop();
            ui_svc_popup_hide();
            rebuild_device_list();
            g_state = BT_MGR_SCAN_DONE;
        }
        break;
    }
    case BT_MGR_PAIRING: {
        /* 保持弹窗可见，等待串口输入 */
        ui_svc_popup("请在串口输入配对码", 100);
        serial_state_t ss = serial_poll();
        if (ss == SERIAL_STATE_PAIR_CODE_RECEIVED) {
            const char *code = serial_get_input();
            const char *name = serial_get_target_name();
            const char *addr = serial_get_target_addr();
            if (code && name) {
                storage_bt_add(addr ? addr : name, name);
                Serial.println("OK");
                ui_svc_notify_success("已配对");
                g_state = BT_MGR_PAIRED;
                rebuild_device_list();
            }
        } else if (ss == SERIAL_STATE_CANCELLED) {
            g_state = BT_MGR_SCAN_DONE;
            ui_svc_notify_info("已取消");
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief 蓝牙开关切换回调
 */
void bt_mgr_on_switch_toggle(void *ud) {
    svc_mgr_handle_switch_toggle(&g_bt_on, bt_mgr_enable, bt_mgr_disable, ud);
}

/* ═══ 内核任务入口 ═══ */

/**
 * @brief BT 管理器内核任务入口
 * @note  每 50ms 轮询一次状态机。
 */
extern "C" void bt_mgr_task_main(void *arg)
{
    (void)arg;
    kern_poll_loop(bt_mgr_update, 50);
}

#endif /* NATIVE_TEST */
