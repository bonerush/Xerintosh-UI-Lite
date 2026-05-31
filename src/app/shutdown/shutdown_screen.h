/**
 * @file   shutdown_screen.h
 * @brief  关机画面头文件
 * @details 复用 Xerintosh logo 绘制关机画面，显示 "GOOD BYE!" 后进入深度睡眠。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SHUTDOWN_SCREEN_H
#define SHUTDOWN_SCREEN_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示关机界面并延时 2 秒
 * @note  复用 boot_screen_draw_logo() 绘制 logo，
 *        底部文字改为 "GOOD BYE!"
 */
void shutdown_screen_show(void);

/**
 * @brief 进入深度睡眠模式
 * @note  硬件环境：调用 ESP32 深度睡眠
 *        native 环境：空操作
 */
void shutdown_screen_power_off(void);

#ifdef __cplusplus
}
#endif

#endif /* SHUTDOWN_SCREEN_H */
