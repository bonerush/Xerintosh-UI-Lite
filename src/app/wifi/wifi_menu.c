/**
 * @file   wifi_menu.c
 * @brief  WiFi 网络菜单构建实现
 * @details 负责动态构建 WiFi 网络子菜单（已保存 / 可用网络 / 扫描按钮）。
 *          本文件仅包含数据结构和菜单构建逻辑，不包含 WiFi 驱动操作。
 *          扫描 SSID 通过 wifi_mgr_get_scan_ssid() 访问器获取。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

/* ═══ NATIVE_TEST 空桩 ═══ */
#include "app/wifi/wifi_menu.h"

xerintosh_list_item_t *g_networks_list       = NULL;
xerintosh_list_item_t *g_settings_list       = NULL;
xerintosh_list_item_t *g_saved_container     = NULL;
xerintosh_list_item_t *g_available_container = NULL;
xerintosh_list_item_t *g_scan_button         = NULL;

void wifi_menu_rebuild_list(int scan_count)
{
    (void)scan_count;
}

#else

#include "app/wifi/wifi_menu.h"
#include "app/storage/storage.h"

/* ═══ 菜单指针（跨文件共享，由 wifi_manager.cpp 初始化/清理） ═══ */

xerintosh_list_item_t *g_networks_list       = NULL;
xerintosh_list_item_t *g_settings_list       = NULL;
xerintosh_list_item_t *g_saved_container     = NULL;
xerintosh_list_item_t *g_available_container = NULL;
xerintosh_list_item_t *g_scan_button         = NULL;

/* ═══ 网络菜单重建 ═══ */

/**
 * @brief 重建网络子菜单（已保存 + 可用网络 + 扫描按钮）
 * @param scan_count 扫描结果数量（可用网络条目数）
 * @note  每次扫描完成或启用 WiFi 时调用，动态更新菜单内容
 */
void wifi_menu_rebuild_list(int scan_count)
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
        xerintosh_list_item_t *connect_btn = xerintosh_new_button_item("连接", wifi_menu_on_saved_connect_pressed, default_icon);
        xerintosh_list_item_t *del_btn = xerintosh_new_button_item("删除", wifi_menu_on_saved_delete_pressed, default_icon);
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
        const char *ssid = wifi_mgr_get_scan_ssid(i);
        if (ssid[0] == '\0') continue; /* 跳过隐藏网络 */
        if (storage_wifi_find(ssid) >= 0) {
            continue; /* 跳过已保存网络 */
        }
        xerintosh_list_item_t *item = xerintosh_new_button_item(ssid, wifi_menu_on_network_button_pressed, default_icon);
        xerintosh_push_item_to_list(g_available_container, item);
    }

    /* ─── 3. 扫描按钮 ─── */
    g_scan_button = xerintosh_new_button_item("扫描", wifi_menu_on_scan_pressed, default_icon);
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

#endif /* NATIVE_TEST */
