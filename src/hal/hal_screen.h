/**
 * @file   hal_screen.h
 * @brief  HAL 屏幕尺寸与方向抽象
 * @details 仅提供屏幕尺寸常量/查询，避免布局模块依赖完整 hal_display.h。
 *          底层在 native 环境使用编译期常量，在硬件环境从显示驱动读取。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_SCREEN_H
#define HAL_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NATIVE_TEST
#define HAL_SCREEN_WIDTH  80
#define HAL_SCREEN_HEIGHT 160
#else
/**
 * @brief 运行时屏幕宽度
 * @note  由 hal_display_init() 在硬件初始化时更新
 */
extern int16_t g_screen_width;

/**
 * @brief 运行时屏幕高度
 */
extern int16_t g_screen_height;

#define HAL_SCREEN_WIDTH  g_screen_width
#define HAL_SCREEN_HEIGHT g_screen_height
#endif

/**
 * @brief 查询当前屏幕尺寸
 * @param w 输出宽度（可为 NULL）
 * @param h 输出高度（可为 NULL）
 */
void hal_screen_get_size(int16_t *w, int16_t *h);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SCREEN_H */
