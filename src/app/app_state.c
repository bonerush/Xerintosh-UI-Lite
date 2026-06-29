/**
 * @file   app_state.c
 * @brief  App 层全局状态定义
 * @details 集中定义 WiFi/BT 开关状态等跨模块全局变量。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_state.h"

/* WiFi 默认开启 */
bool g_wifi_on = true;

/* 蓝牙默认关闭（与 WiFi 互斥，需手动开启） */
bool g_bt_on = false;
