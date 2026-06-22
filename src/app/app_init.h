/**
 * @file   app_init.h
 * @brief  App 初始化与输入处理头文件
 * @details 声明 UI 菜单构建、管理器初始化及每帧输入处理接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef APP_INIT_H
#define APP_INIT_H

#include <stdbool.h>
#include <stdint.h>

#include "app/app_state.h"
#include "app/app_menu.h"
#include "app/app_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 构建并初始化 Xerintosh UI 菜单树
 * @note  实际实现位于 app_menu.c；此处保留入口以保持 main.cpp 不变。
 */
void app_init_ui(void);

/**
 * @brief 初始化各管理器（WiFi），并根据存储状态自动启用
 */
void app_init_managers(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
