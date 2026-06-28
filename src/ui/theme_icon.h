/**
 * @file   theme_icon.h
 * @brief  自定义图标位图 — 由 icon_converter.py 自动生成
 * @note   尺寸: 8x8 像素，XBM 格式
 *
 * 使用方法:
 *   #include "theme_icon.h"
 *   item->icon = custom_icon;
 *   item->bitmap_data = icon_theme_bitmap;
 *   item->bitmap_w = 8;
 *   item->bitmap_h = 8;
 */

#ifndef ICON_THEME_H
#define ICON_THEME_H

#include <stdint.h>

#define ICON_THEME_WIDTH  8
#define ICON_THEME_HEIGHT 8

static const uint8_t icon_theme_bitmap[] = {
    0x3C, 0x7E, 0x9F, 0x8F, 0x8F, 0x9F, 0x7E, 0x3C
};

#endif /* ICON_THEME_H */
