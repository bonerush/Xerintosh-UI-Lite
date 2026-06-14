/**
 * @file   app_init.c
 * @brief  App 初始化入口封装
 * @details 提供向后兼容的 `app_init_ui()` / `app_init_managers()` /
 *          `app_input_process()` 入口，实际实现分别位于
 *          app_menu.c、app_input.c 与各管理器模块。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_init.h"

#include "app_menu.h"
#include "app_input.h"
#include "settings/settings.h"
#include "wifi/wifi_manager.h"
#include "bluetooth/bt_manager.h"
#include "app/shutdown/power_key_popup.h"

void app_init_ui(void)
{
    app_menu_build();
}

void app_init_managers(void)
{
    bt_mgr_init();
    wifi_mgr_init();
    power_key_popup_init();

    /* 先初始化蓝牙（Classic BT SPP），再给 WiFi 初始化，
       避免 WiFi 占用大量 RAM 后 Bluedroid BLE 初始化分配失败
       触发 ESP-IDF v4.4 的 vQueueDelete(NULL) bug */
    /* BT 初始化已移至 deferred_kernel_init()（内核任务 spawn 之后），
       避免在 setup() 中过早消耗内存导致 FreeRTOS 任务创建失败。 */
    if (g_wifi_on) wifi_mgr_enable();

    /* WiFi/BT 内核任务由 setup() 在 xerintosh_init_core() 之后启动，
       确保 g_xerintosh_selector 已初始化，避免 LoadStoreError */
}
