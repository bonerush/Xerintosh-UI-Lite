/**
 * @file   ui_draw_widgets.c
 * @brief  UI 控件绘制实现（信息栏 + 弹窗）
 * @details 实现顶部信息栏和中部弹窗的绘制逻辑。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_drawer.h"
#include "ui_item.h"
#include "ui_core.h"
#include "hal/hal_system.h"

/* ═══ 信息栏 ═══ */

/**
 * @brief 绘制顶部信息栏
 * @note  包含三层圆角矩形堆叠形成立体边框，内部显示文字
 */
void xerintosh_draw_info_bar()
{
  if (!g_xerintosh_info_bar.is_running) return;

  /* 到达目标位置后记录时间戳，用于判断是否超时 */
  if (g_xerintosh_info_bar.y_info_bar == g_xerintosh_info_bar.y_info_bar_trg)
    g_xerintosh_info_bar.time = hal_get_ticks();

  /* 超时后向屏幕上方移出 */
  if (g_xerintosh_info_bar.time - g_xerintosh_info_bar.time_start >= g_xerintosh_info_bar.span)
  {
    g_xerintosh_info_bar.y_info_bar_trg = 0 - 2 * INFO_BAR_HEIGHT;
    if (g_xerintosh_info_bar.y_info_bar == g_xerintosh_info_bar.y_info_bar_trg)
      g_xerintosh_info_bar.is_running = false;
  }

  int16_t _x_info_bar = SCREEN_WIDTH/2 - g_xerintosh_info_bar.w_info_bar/2;
  int16_t _y_info_bar_1 = g_xerintosh_info_bar.y_info_bar - 4;
  int16_t _y_info_bar_2 = g_xerintosh_info_bar.y_info_bar + INFO_BAR_HEIGHT;

  xerintosh_set_font(hal_get_cn_font());

  /* 第一层：阴影底色 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_info_bar + 3, _y_info_bar_1 + 3,
                  (int16_t)g_xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 4, g_xerintosh_draw_color);

  /* 第二层：外框 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_info_bar.w_info_bar + 4)/2), _y_info_bar_1,
                  (int16_t)(g_xerintosh_info_bar.w_info_bar + 4), INFO_BAR_HEIGHT + 6, 4, g_xerintosh_draw_color);

  /* 第三层：内框 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_info_bar, _y_info_bar_1,
                  (int16_t)g_xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 3, g_xerintosh_draw_color);

  /* 高光与文字 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(_x_info_bar + 2, _y_info_bar_2 - 2, (int16_t)(g_xerintosh_info_bar.w_info_bar - 4), g_xerintosh_draw_color);
  hal_draw_pixel(_x_info_bar + 1, _y_info_bar_2 - 3, g_xerintosh_draw_color);
  hal_draw_pixel(_x_info_bar - 2, _y_info_bar_2 - 3, g_xerintosh_draw_color);

  hal_draw_string(_x_info_bar + 6,
                 (int16_t)(g_xerintosh_info_bar.y_info_bar + hal_get_font_height() - 2),
                 g_xerintosh_info_bar.content, g_xerintosh_draw_color);
}

/* ═══ 弹窗 ═══ */

/**
 * @brief 根据换行数计算弹窗高度
 * @param wrap_line_count 内容行数
 * @return 弹窗总高度（含 padding）
 */
static int16_t popup_compute_height(uint8_t wrap_line_count)
{
  int16_t fh = hal_get_font_height();
  uint8_t n = wrap_line_count;
  if (n < 1) n = 1;
  if (n > POP_UP_WRAP_LINES) n = POP_UP_WRAP_LINES;
  int16_t content_h = (int16_t)(n * fh + (n - 1) * 2);
  int16_t pop_h = content_h + 8;
  if (pop_h < 24) pop_h = 24;
  return pop_h;
}

/**
 * @brief 绘制中部弹窗
 * @note  与信息栏类似的三层圆角矩形堆叠，居中显示提示文字
 */
void xerintosh_draw_pop_up()
{
  if (!g_xerintosh_pop_up.is_running) return;

  /* 到达目标位置后记录时间戳 */
  if (g_xerintosh_pop_up.y_pop_up == g_xerintosh_pop_up.y_pop_up_trg)
    g_xerintosh_pop_up.time = hal_get_ticks();

  /* 超时后向上移出 */
  if (g_xerintosh_pop_up.time - g_xerintosh_pop_up.time_start >= g_xerintosh_pop_up.span)
  {
    g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
    if (g_xerintosh_pop_up.y_pop_up == g_xerintosh_pop_up.y_pop_up_trg)
      g_xerintosh_pop_up.is_running = false;
  }

  /* 根据实际内容计算动态高度：文字高度 + 上下各 4px padding */
  int16_t pop_h = popup_compute_height(g_xerintosh_pop_up.wrap_line_count);
  int16_t fh = hal_get_font_height();
  uint8_t n = g_xerintosh_pop_up.wrap_line_count;
  if (n < 1) n = 1;
  if (n > POP_UP_WRAP_LINES) n = POP_UP_WRAP_LINES;
  int16_t content_h = (int16_t)(n * fh + (n - 1) * 2);

  int16_t _x_pop_up = SCREEN_WIDTH/2 - g_xerintosh_pop_up.w_pop_up/2;
  int16_t _y_pop_up = g_xerintosh_pop_up.y_pop_up + pop_h;

  xerintosh_set_font(hal_get_cn_font());

  /* 外框 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_round_rect((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_pop_up.w_pop_up + 4)/2 - 2), (int16_t)(g_xerintosh_pop_up.y_pop_up - 2),
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 8), pop_h + 4, 5, g_xerintosh_draw_color);

  /* 内框 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_round_rect(_x_pop_up - 2, (int16_t)g_xerintosh_pop_up.y_pop_up,
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 4), pop_h, 3, g_xerintosh_draw_color);

  /* 高光与文字 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(_x_pop_up, _y_pop_up - 2, (int16_t)g_xerintosh_pop_up.w_pop_up, g_xerintosh_draw_color);
  hal_draw_pixel(_x_pop_up - 1, _y_pop_up - 3, g_xerintosh_draw_color);
  hal_draw_pixel((int16_t)(SCREEN_WIDTH/2 + g_xerintosh_pop_up.w_pop_up/2), _y_pop_up - 3, g_xerintosh_draw_color);

  /* 多行文字渲染 */
  {
    int16_t y_text = (int16_t)(g_xerintosh_pop_up.y_pop_up + (pop_h - content_h) / 2 + fh - 2);

    for (uint8_t i = 0; i < n; i++)
    {
      hal_draw_string(_x_pop_up + 3, y_text,
                      g_xerintosh_pop_up.wrap_lines[i], g_xerintosh_draw_color);
      y_text += fh + 2;
    }
  }
}

/* ═══ 控件聚合 ═══

 * @note  xerintosh_draw_widget() 已内联到 xerintosh_ui_widget_core() 中，
 *        直接调用 xerintosh_draw_info_bar() 和 xerintosh_draw_pop_up()。
 */
