/**
 * @file   boot_screen.h
 * @brief  开机画面头文件
 * @details 显示 "Xerintosh" 品牌开机画面，包含复古设备轮廓绘制。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示开机画面并延时 2 秒
 * @note  绘制白色圆角矩形机身、黑色屏幕区域、软盘槽及品牌文字
 */
void boot_screen_show(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_SCREEN_H */
