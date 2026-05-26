/**
 * @file   sm_ui.h
 * @brief  串口监视器绘制头文件
 * @details 声明串口监视器的界面绘制入口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SM_UI_H
#define SM_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 绘制串口监视器完整界面（信息栏 + 终端）
 */
void serial_monitor_draw(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_UI_H */
