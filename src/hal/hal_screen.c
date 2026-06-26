/**
 * @file   hal_screen.c
 * @brief  HAL 屏幕尺寸查询实现
 * @details 提供跨平台的屏幕尺寸读取入口。
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_screen.h"
#include <stddef.h>

#ifndef NATIVE_TEST
int16_t g_screen_width = 80;
int16_t g_screen_height = 160;
#endif

void hal_screen_get_size(int16_t *w, int16_t *h)
{
    if (w != NULL) *w = HAL_SCREEN_WIDTH;
    if (h != NULL) *h = HAL_SCREEN_HEIGHT;
}
