/**
 * @file   ui_draw_icons.c
 * @brief  UI 图标绘制实现
 * @details 实现菜单列表项的图标绘制，包括内置图标和自定义位图图标。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_drawer.h"
#include <stddef.h>

/* ═══ 图标绘制 ═══ */

/**
 * @brief 绘制指定类型的图标
 * @param icon 图标类型
 * @param x    图标左上角 x 坐标
 * @param y    图标中心 y 坐标
 */
void xerintosh_draw_list_icon(xerintosh_list_item_icon_t icon, uint16_t x, uint16_t y){
  switch(icon){
    case (list_icon):
        hal_draw_h_line(2 + x, y - 2, 4, g_xerintosh_draw_color);
        hal_draw_h_line(2 + x, y, 5, g_xerintosh_draw_color);
        hal_draw_h_line(2 + x, y + 2, 3, g_xerintosh_draw_color);
      break;
    case (switch_icon):
        hal_draw_circle(4 + x, y + 1, 3, g_xerintosh_draw_color);
        hal_draw_v_line(4 + x, y, 3, g_xerintosh_draw_color);
      break;
    case (plus_icon):
        hal_draw_circle(4 + x, y + 1, 3, g_xerintosh_draw_color);
        hal_draw_v_line(4 + x, y, 3, g_xerintosh_draw_color);
        hal_draw_h_line(3 + x, y + 1, 3, g_xerintosh_draw_color);
      break;
    case (slider_icon):
        hal_draw_v_line(3 + x, y - 1, 5, g_xerintosh_draw_color);
        hal_draw_v_line(6 + x, y - 1, 5, g_xerintosh_draw_color);
        hal_draw_fill_rect(2 + x, y - 2, 3, 3, g_xerintosh_draw_color);
        hal_draw_fill_rect(5 + x, y + 2, 3, 3, g_xerintosh_draw_color);
      break;
    case (user_icon):
        hal_draw_string(2 + x, y + hal_get_font_height() / 2, "-", g_xerintosh_draw_color);
      break;
    case (flag_icon):
        hal_draw_v_line(6 + x, y - 1, 5, g_xerintosh_draw_color);
        hal_draw_fill_rect(3 + x, y - 2, 4, 3, g_xerintosh_draw_color);
      break;
    case (power_icon):
        hal_draw_circle(4 + x, y + 1, 3, g_xerintosh_draw_color);
        hal_draw_v_line(4 + x, y - 2, 3, g_xerintosh_draw_color);
        g_xerintosh_draw_color = COLOR_BG;
        hal_draw_pixel(x+3, y-2, g_xerintosh_draw_color);
        hal_draw_pixel(x+5, y-2, g_xerintosh_draw_color);
        g_xerintosh_draw_color = COLOR_FG;
      break;
    default:
      break;
  }
}

/**
 * @brief  绘制列表项的自定义位图图标
 * @param  _item 列表项指针（需已设置 bitmap_data）
 * @param  x     图标左上角 x 坐标
 * @param  y     图标中心 y 坐标
 * @note   当 icon 为 custom_icon 时由 draw_list_item_xxx 调用
 */
void xerintosh_draw_item_bitmap(xerintosh_list_item_t *_item, uint16_t x, uint16_t y)
{
  if (_item == NULL || _item->bitmap_data == NULL || _item->bitmap_w == 0 || _item->bitmap_h == 0)
    return;

  /* 将图标居中于给定的 (x, y) 位置（y 为图标中心） */
  int16_t draw_x = x;
  int16_t draw_y = y - _item->bitmap_h / 2;
  hal_draw_xbitmap(draw_x, draw_y, _item->bitmap_w, _item->bitmap_h, _item->bitmap_data);
}
