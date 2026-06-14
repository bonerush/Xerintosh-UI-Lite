/**
 * @file   app_state.c
 * @brief  App 层全局状态定义
 * @details 集中定义 WiFi/BT 开关状态等跨模块全局变量。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_state.h"

/* WiFi 默认开启（BT 默认关闭，内存充足） */
bool g_wifi_on = true;
bool g_bt_on   = false;
