/**
 * @file   ui_draw_anim.c
 * @brief  UI 退场动画实现
 * @details 绘制退场遮罩动画（沙漏 + 扫描线效果）。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_drawer.h"
#include "ui_core.h"

/* ═══ 全局状态定义 ═══ */

uint8_t g_xerintosh_exit_animation_status = 0;  /* 退场动画阶段状态机：0=初始/展开，1=到达底部，2=回缩 */

/* ═══ 退场动画 ═══ */

/**
 * @brief 绘制退场遮罩动画（沙漏 + 扫描线效果）
 * @note  使用静态变量 _temp_h 控制遮罩高度，经历三个阶段：
 *        阶段 0：从 -8 向下展开到 SCREEN_HEIGHT+8
 *        阶段 1：到达底部后触发状态切换
 *        阶段 2：回缩到 -8 并标记动画完成
 */
void xerintosh_draw_exit_animation()
{
  static float _temp_h = -8;
  static float _temp_h_trg = -999;
  static bool _last_finished = true;

  /* 每次新动画开始时强制重置状态，避免 static 变量残留导致卡死 */
  if (_last_finished && !g_xerintosh_exit_animation_finished)
  {
    _temp_h = -8;
    _temp_h_trg = SCREEN_HEIGHT + 8;
    g_xerintosh_exit_animation_status = 0;
  }
  _last_finished = g_xerintosh_exit_animation_finished;

  /* 屏幕方向/尺寸切换时，防止 _temp_h/_temp_h_trg 残留旧尺寸的目标值导致动画异常 */
  static int16_t _prev_screen_height = -1;
  if (_prev_screen_height != SCREEN_HEIGHT)
  {
    float max_h = SCREEN_HEIGHT + 8;
    if (_temp_h > max_h) _temp_h = max_h;
    if (g_xerintosh_exit_animation_status == 0) _temp_h_trg = max_h;
    _prev_screen_height = SCREEN_HEIGHT;
  }

  /* 绘制全屏黑色遮罩，高度由 _temp_h 控制 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_rect(0, 0, SCREEN_WIDTH, _temp_h, g_xerintosh_draw_color);
  g_xerintosh_draw_color = COLOR_FG;

  /* 计算沙漏居中偏移 */
  uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
  int8_t _y_hourglass = _temp_h - SCREEN_HEIGHT / 2 - 18;
  if (_y_hourglass + 20 >= 0)
  {
    /* 沙漏上半部分 */
    hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 2, 13, 3, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 3, 9, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_FG;

    hal_draw_v_line(_x_hourglass_offset + 1, _y_hourglass + 4, 5, g_xerintosh_draw_color);
    hal_draw_v_line(_x_hourglass_offset + 11, _y_hourglass + 4, 5, g_xerintosh_draw_color);

    /* 沙漏中间填充 */
    for (uint8_t i = 0; i < 5; ++i)
    {
      int8_t _current_y = _y_hourglass + 8 + i;
      int8_t _left_x = (i < 3) ? (_x_hourglass_offset + 1 + i) : (_x_hourglass_offset + 4);
      int8_t _right_x = (i < 3) ? (_x_hourglass_offset + 10 - i) : (_x_hourglass_offset + 7);
      hal_draw_h_line(_left_x, _current_y, 2, g_xerintosh_draw_color);
      hal_draw_h_line(_right_x, _current_y, 2, g_xerintosh_draw_color);
    }

    for (uint8_t i = 0; i < 3; ++i)
    {
      int8_t _current_y = _y_hourglass + 13 + i;
      hal_draw_h_line(_x_hourglass_offset + 3 - i, _current_y, 2, g_xerintosh_draw_color);
      hal_draw_h_line(_x_hourglass_offset + 8 + i, _current_y, 2, g_xerintosh_draw_color);
    }

    hal_draw_v_line(_x_hourglass_offset + 1, _y_hourglass + 16, 3, g_xerintosh_draw_color);
    hal_draw_v_line(_x_hourglass_offset + 11, _y_hourglass + 16, 3, g_xerintosh_draw_color);

    /* 沙漏下半部分 */
    hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 19, 13, 3, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 20, 9, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_FG;

    /* 沙漏颗粒点 */
    const uint8_t _points[][2] = {
      {5, 7}, {7, 7}, {6, 8}, {6, 10}, {6, 14}, {6, 16},
      {5, 17}, {7, 17}, {4, 18}, {6, 18}, {8, 18}
    };
    for (uint8_t i = 0; i < sizeof(_points) / sizeof(_points[0]); ++i)
      hal_draw_pixel(_x_hourglass_offset + _points[i][0], _y_hourglass + _points[i][1], g_xerintosh_draw_color);
  }

  /* 底部扫描线 */
  if (_temp_h + 3 >= 0)
    for (uint8_t i = 0; i <= 3; ++i)
      hal_draw_h_line(0, _temp_h + i, SCREEN_WIDTH, g_xerintosh_draw_color);

  /* 交错像素扫描效果 */
  for (int16_t i = 0; i <= SCREEN_WIDTH; i += 2)
    for (int16_t j = _temp_h - 5; j <= _temp_h - 1; j++)
    {
      if (j % 2 == 0)
        hal_draw_pixel(i + 1, j, g_xerintosh_draw_color);
      if (j % 2 == 1)
        hal_draw_pixel(i, j, g_xerintosh_draw_color);
    }

  xerintosh_animation(&_temp_h, _temp_h_trg, ANIM_SPEED_EXIT);

  /* 状态机转换：使用范围判断代替浮点精确相等，避免多次循环后卡死 */
  if (g_xerintosh_exit_animation_status == 0 && _temp_h >= SCREEN_HEIGHT + 8 - 1.0f)
  {
    _temp_h = SCREEN_HEIGHT + 8;
    g_xerintosh_exit_animation_status = 1;
    return;
  }

  if (g_xerintosh_exit_animation_status == 1)
  {
    _temp_h_trg = -8;
    g_xerintosh_exit_animation_status = 2;
    return;
  }

  if (g_xerintosh_exit_animation_status == 2 && _temp_h <= -8 + 1.0f)
  {
    g_xerintosh_exit_animation_finished = true;
    g_xerintosh_exit_animation_status = 0;
    _temp_h = -8;
    _temp_h_trg = SCREEN_HEIGHT + 8;
    return;
  }
}
