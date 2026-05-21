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

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 外部状态标志（由 main.cpp / native_main.cpp 定义）═══ */

extern bool wifi_on;  /* WiFi 开关状态 */
extern bool bt_on;    /* 蓝牙开关状态 */

/* ═══ 生命周期 ═══ */

/**
 * @brief 构建并初始化 Xerintosh UI 菜单树
 * @note  创建根菜单及设置/关于子菜单，挂载开关、滑块等控件
 */
void app_init_ui(void);

/**
 * @brief 初始化各管理器（WiFi、蓝牙），并根据存储状态自动启用
 */
void app_init_managers(void);

/* ═══ 每帧输入处理 ═══ */

/**
 * @brief 处理按键输入事件，映射到 UI 选择器操作
 * @note  按键 B：短按=上一项，长按=退出/取消
 *        按键 A：短按=下一项，长按=确认/进入
 */
void app_input_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
