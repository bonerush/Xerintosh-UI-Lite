/**
 * @file   boot_screen.h
 * @brief  开机画面头文件
 * @details 显示 "Xerintosh" 品牌开机画面，包含复古设备轮廓绘制。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示开机画面并延时 2 秒
 * @note  绘制白色圆角矩形机身、黑色屏幕区域、软盘槽及品牌文字
 */
void boot_screen_show(void);

/**
 * @brief  在指定位置绘制 Xerintosh 设备 logo
 * @param  ox    左上角 X 坐标
 * @param  oy    左上角 Y 坐标
 * @param  scale 缩放比例（百分比，100 = 原始大小）
 * @note   供开机画面和关于页面共用
 */
void boot_screen_draw_logo(int16_t ox, int16_t oy, int16_t scale);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_SCREEN_H */
