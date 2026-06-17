/**
 * @file   wifi_menu.h
 * @brief  WiFi 网络菜单构建头文件
 * @details 提供网络菜单动态构建接口。本模块仅包含数据结构和菜单构建逻辑，
 *          不包含 WiFi 驱动操作。回调函数由 wifi_manager.cpp 定义。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef WIFI_MENU_H
#define WIFI_MENU_H

#include "ui/ui_item.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 菜单指针（跨文件共享） ═══ */

extern xerintosh_list_item_t *g_networks_list;       /* "网络" 列表项（设置的子项） */
extern xerintosh_list_item_t *g_settings_list;       /* "设置" 列表项 */
extern xerintosh_list_item_t *g_saved_container;     /* "已保存" 容器 */
extern xerintosh_list_item_t *g_available_container; /* "可用网络" 容器 */
extern xerintosh_list_item_t *g_scan_button;         /* "扫描" 按钮 */

/* ═══ 回调函数（由 wifi_manager.cpp 定义） ═══ */

extern void wifi_menu_on_network_button_pressed(void *ud);
extern void wifi_menu_on_saved_connect_pressed(void *ud);
extern void wifi_menu_on_saved_delete_pressed(void *ud);
extern void wifi_menu_on_scan_pressed(void *ud);

/* ═══ 扫描 SSID 访问器（由 wifi_manager.cpp 定义） ═══ */

/**
 * @brief  获取指定索引的扫描结果 SSID
 * @param  index 扫描结果索引（0-based）
 * @return SSID 字符串指针（静态缓冲区，每次调用覆盖）
 * @note   供 wifi_menu.c 在 C 环境中替代 WiFi.SSID() 调用
 */
extern const char *wifi_mgr_get_scan_ssid(int index);

/* ═══ 菜单重建 ═══ */

/**
 * @brief 重建网络子菜单（已保存 + 可用网络 + 扫描按钮）
 * @param scan_count 扫描结果数量（可用网络条目数）
 * @note  每次扫描完成或启用 WiFi 时调用，动态更新菜单内容
 */
void wifi_menu_rebuild_list(int scan_count);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MENU_H */
