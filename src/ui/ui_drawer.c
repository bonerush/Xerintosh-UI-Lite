/**
 * @file   ui_drawer.c
 * @brief  UI 渲染管线实现
 * @details 实现退场动画、信息栏、弹窗、列表外观、列表项、图标、
 *          选择器高亮框、长按提示及文字滚动等全部绘制逻辑。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_drawer.h"

#include <math.h>
#include <stdio.h>

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
  if (_temp_h_trg < 0) _temp_h_trg = SCREEN_HEIGHT + 8;

  /* 绘制全屏黑色遮罩，高度由 _temp_h 控制 */
  oled_set_draw_color(0);
  oled_draw_box(0, 0, SCREEN_WIDTH, _temp_h);
  oled_set_draw_color(1);

  /* 计算沙漏居中偏移 */
  uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
  int8_t _y_hourglass = _temp_h - SCREEN_HEIGHT / 2 - 18;
  if (_y_hourglass + 20 >= 0)
  {
    /* 沙漏上半部分 */
    oled_draw_box(_x_hourglass_offset, _y_hourglass + 2, 13, 3);
    oled_set_draw_color(0);
    oled_draw_H_line(_x_hourglass_offset + 2, _y_hourglass + 3, 9);
    oled_set_draw_color(1);

    oled_draw_V_line(_x_hourglass_offset + 1, _y_hourglass + 4, 5);
    oled_draw_V_line(_x_hourglass_offset + 11, _y_hourglass + 4, 5);

    /* 沙漏中间填充 */
    for (uint8_t i = 0; i < 5; ++i)
    {
      int8_t _current_y = _y_hourglass + 8 + i;
      int8_t _left_x = (i < 3) ? (_x_hourglass_offset + 1 + i) : (_x_hourglass_offset + 4);
      int8_t _right_x = (i < 3) ? (_x_hourglass_offset + 10 - i) : (_x_hourglass_offset + 7);
      oled_draw_H_line(_left_x, _current_y, 2);
      oled_draw_H_line(_right_x, _current_y, 2);
    }

    for (uint8_t i = 0; i < 3; ++i)
    {
      int8_t _current_y = _y_hourglass + 13 + i;
      oled_draw_H_line(_x_hourglass_offset + 3 - i, _current_y, 2);
      oled_draw_H_line(_x_hourglass_offset + 8 + i, _current_y, 2);
    }

    oled_draw_V_line(_x_hourglass_offset + 1, _y_hourglass + 16, 3);
    oled_draw_V_line(_x_hourglass_offset + 11, _y_hourglass + 16, 3);

    /* 沙漏下半部分 */
    oled_draw_box(_x_hourglass_offset, _y_hourglass + 19, 13, 3);
    oled_set_draw_color(0);
    oled_draw_H_line(_x_hourglass_offset + 2, _y_hourglass + 20, 9);
    oled_set_draw_color(1);

    /* 沙漏颗粒点 */
    const uint8_t _points[][2] = {
      {5, 7}, {7, 7}, {6, 8}, {6, 10}, {6, 14}, {6, 16},
      {5, 17}, {7, 17}, {4, 18}, {6, 18}, {8, 18}
    };
    for (uint8_t i = 0; i < sizeof(_points) / sizeof(_points[0]); ++i)
      oled_draw_pixel(_x_hourglass_offset + _points[i][0], _y_hourglass + _points[i][1]);
  }

  /* 底部扫描线 */
  if (_temp_h + 3 >= 0)
    for (uint8_t i = 0; i <= 3; ++i)
      oled_draw_H_line(0, _temp_h + i, SCREEN_WIDTH);

  /* 交错像素扫描效果 */
  for (int16_t i = 0; i <= SCREEN_WIDTH; i += 2)
    for (int16_t j = _temp_h - 5; j <= _temp_h - 1; j++)
    {
      if (j % 2 == 0)
        oled_draw_pixel(i + 1, j);
      if (j % 2 == 1)
        oled_draw_pixel(i, j);
    }

  xerintosh_animation(&_temp_h, _temp_h_trg, ANIM_SPEED_EXIT);

  /* 状态机转换 */
  if (g_xerintosh_exit_animation_status == 0 && _temp_h == _temp_h_trg && _temp_h == SCREEN_HEIGHT + 8)
  {
    g_xerintosh_exit_animation_status = 1;
    return;
  }

  if (g_xerintosh_exit_animation_status == 1)
  {
    _temp_h_trg = -8;
    g_xerintosh_exit_animation_status = 2;
    return;
  }

  if (g_xerintosh_exit_animation_status == 2 && _temp_h == _temp_h_trg && _temp_h == -8)
  {
    g_xerintosh_exit_animation_finished = true;
    g_xerintosh_exit_animation_status = 0;
    _temp_h = -8;
    _temp_h_trg = SCREEN_HEIGHT + 8;
    return;
  }
}

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
    g_xerintosh_info_bar.time = get_ticks();

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
  oled_set_draw_color(1);
  oled_draw_R_box(_x_info_bar + 3, _y_info_bar_1 + 3,
                  (int16_t)g_xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 4);

  /* 第二层：外框 */
  oled_set_draw_color(0);
  oled_draw_R_box((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_info_bar.w_info_bar + 4)/2), _y_info_bar_1,
                  (int16_t)(g_xerintosh_info_bar.w_info_bar + 4), INFO_BAR_HEIGHT + 6, 4);

  /* 第三层：内框 */
  oled_set_draw_color(1);
  oled_draw_R_box(_x_info_bar, _y_info_bar_1,
                  (int16_t)g_xerintosh_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 3);

  /* 高光与文字 */
  oled_set_draw_color(0);
  oled_draw_H_line(_x_info_bar + 2, _y_info_bar_2 - 2, (int16_t)(g_xerintosh_info_bar.w_info_bar - 4));
  oled_draw_pixel(_x_info_bar + 1, _y_info_bar_2 - 3);
  oled_draw_pixel(_x_info_bar - 2, _y_info_bar_2 - 3);

  oled_draw_UTF8(_x_info_bar + 6,
                 (int16_t)(g_xerintosh_info_bar.y_info_bar + oled_get_str_height() - 2),
                 g_xerintosh_info_bar.content);
}

/* ═══ 弹窗 ═══ */

/**
 * @brief 绘制中部弹窗
 * @note  与信息栏类似的三层圆角矩形堆叠，居中显示提示文字
 */
void xerintosh_draw_pop_up()
{
  if (!g_xerintosh_pop_up.is_running) return;

  /* 到达目标位置后记录时间戳 */
  if (g_xerintosh_pop_up.y_pop_up == g_xerintosh_pop_up.y_pop_up_trg)
    g_xerintosh_pop_up.time = get_ticks();

  /* 超时后向上移出 */
  if (g_xerintosh_pop_up.time - g_xerintosh_pop_up.time_start >= g_xerintosh_pop_up.span)
  {
    g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * INFO_BAR_HEIGHT;
    if (g_xerintosh_pop_up.y_pop_up == g_xerintosh_pop_up.y_pop_up_trg)
      g_xerintosh_pop_up.is_running = false;
  }

  int16_t _x_pop_up = SCREEN_WIDTH/2 - g_xerintosh_pop_up.w_pop_up/2;
  int16_t _y_pop_up = g_xerintosh_pop_up.y_pop_up + POP_UP_HEIGHT;

  xerintosh_set_font(hal_get_cn_font());

  /* 阴影底色 */
  oled_set_draw_color(1);
  oled_draw_R_box(_x_pop_up + 1, (int16_t)g_xerintosh_pop_up.y_pop_up + 3,
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 4), POP_UP_HEIGHT, 4);

  /* 外框 */
  oled_set_draw_color(0);
  oled_draw_R_box((int16_t)(SCREEN_WIDTH/2 - (g_xerintosh_pop_up.w_pop_up + 4)/2 - 2), (int16_t)(g_xerintosh_pop_up.y_pop_up - 2),
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 8), POP_UP_HEIGHT + 4, 5);

  /* 内框 */
  oled_set_draw_color(1);
  oled_draw_R_box(_x_pop_up - 2, (int16_t)g_xerintosh_pop_up.y_pop_up,
                  (int16_t)(g_xerintosh_pop_up.w_pop_up + 4), POP_UP_HEIGHT, 3);

  /* 高光与文字 */
  oled_set_draw_color(0);
  oled_draw_H_line(_x_pop_up, _y_pop_up - 2, (int16_t)g_xerintosh_pop_up.w_pop_up);
  oled_draw_pixel(_x_pop_up - 1, _y_pop_up - 3);
  oled_draw_pixel((int16_t)(SCREEN_WIDTH/2 + g_xerintosh_pop_up.w_pop_up/2), _y_pop_up - 3);

  oled_draw_UTF8(_x_pop_up + 3,
                 (int16_t)(g_xerintosh_pop_up.y_pop_up + oled_get_str_height() + 1),
                 g_xerintosh_pop_up.content);
}

/* ═══ 列表外观 ═══ */

/**
 * @brief 绘制列表整体外观（边框、滚动条、装饰像素）
 * @note  顶部装饰横线 + 右侧滚动指示条（显示当前选中位置比例）
 */
void xerintosh_draw_list_appearance()
{
  oled_set_draw_color(1);
  oled_draw_H_line(0, 1, 66);
  oled_draw_H_line(0, 0, 67);

  /* 顶部右侧装饰像素 */
  const struct
  {
    uint8_t _start;
    uint8_t _end;
    uint8_t _step;
    uint8_t _y;
  } draw_cfg[] = {
      {67, 99, 2, 1},
      {68, 100, 2, 0},
      {102, 111, 3, 1},
      {103, 112, 3, 0},
      {115, 124, 5, 1},
      {116, 124, 5, 0}
    };

  for (uint8_t j = 0; j < sizeof(draw_cfg) / sizeof(draw_cfg[0]); ++j)
    for (uint8_t i = draw_cfg[j]._start; i <= draw_cfg[j]._end; i += draw_cfg[j]._step)
      oled_draw_pixel(i, draw_cfg[j]._y);

  /* 右侧边框竖线 */
  oled_draw_V_line(SCREEN_WIDTH - 5, 0, SCREEN_HEIGHT);
  oled_draw_V_line(SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT);

  /* 滚动条 */
  static float _length_each_part = 0;
  _length_each_part = ceilf((SCREEN_HEIGHT - 10.0f) / (float) g_xerintosh_selector.selected_item->parent->child_num);
  oled_draw_box(SCREEN_WIDTH - 4, 5 + g_xerintosh_selector.selected_index * _length_each_part, 3, _length_each_part);

  /* 滚动条内部高光线 */
  oled_set_draw_color(0);
  oled_draw_H_line(SCREEN_WIDTH - 4, _length_each_part + (float)g_xerintosh_selector.selected_index * _length_each_part, 3);

  if (_length_each_part >= 9)
  {
    oled_draw_H_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part - 2.0f + (float)g_xerintosh_selector.selected_index * _length_each_part), 3);
    oled_draw_H_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part + 2.0f + (float)g_xerintosh_selector.selected_index * _length_each_part), 3);
  }

  /* 滚动条上下端点 */
  oled_set_draw_color(1);
  oled_draw_box(SCREEN_WIDTH - 4, 0, 3, 4);
  oled_draw_box(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, 3, 4);
  oled_set_draw_color(0);
  oled_draw_H_line(SCREEN_WIDTH - 4, 2, 3);
  oled_draw_pixel(SCREEN_WIDTH - 3, 1);
  oled_draw_H_line(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 3, 3);
  oled_draw_pixel(SCREEN_WIDTH - 3, SCREEN_HEIGHT - 2);
}

/* ═══ 可见性判断 ═══ */

/**
 * @brief 判断列表项是否在屏幕可视区域内
 * @param _y_item 项的 y 坐标
 * @return true  可见
 * @return false 不可见
 */
static bool is_item_visible(int16_t _y_item)
{
  return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}

/* ═══ 各类列表项绘制 ═══ */

/**
 * @brief 绘制普通列表项（仅图标）
 */
static void draw_list_item_list(xerintosh_list_item_t *_item, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    xerintosh_draw_list_icon(_item->icon, _x, _y);
}

/**
 * @brief 绘制开关项（图标 + 右侧开关控件）
 */
static void draw_list_item_switch(xerintosh_switch_item_t *_switch, int16_t _x, int16_t _y)
{
  if (_switch->init_function && g_xerintosh_refresh_list_value)
    _switch->init_function();
  if (!is_item_visible(_y)) return;

  xerintosh_draw_list_icon(_switch->base_item.icon, _x, _y);

  /* 绘制开关外框 */
  oled_draw_frame(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 7, _y - 2, 11, 7);
  if (*_switch->value)
  {
    /* 开启态：方块靠右 */
    oled_draw_box(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 1, _y, 3, 3);
    oled_draw_pixel(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 4, _y + 1);
  }
  else
  {
    /* 关闭态：方块靠左 */
    oled_draw_box(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 5, _y, 3, 3);
    oled_draw_pixel(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN, _y + 1);
  }
}

/**
 * @brief 绘制按钮项（仅图标）
 */
static void draw_list_item_button(xerintosh_button_item_t *_button, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    xerintosh_draw_list_icon(_button->base_item.icon, _x, _y);
}

/**
 * @brief 绘制滑块项（图标 + 右侧数值/滑块控件）
 */
static void draw_list_item_slider(xerintosh_slider_item_t *_slider, int16_t _x, int16_t _y)
{
  if (_slider->init_function && g_xerintosh_refresh_list_value)
    _slider->init_function();
  if (!is_item_visible(_y)) return;

  xerintosh_draw_list_icon(_slider->base_item.icon, _x, _y);

  /* 已确认的滑块值在选择器 XOR 绘制之后再显示，避免反色伪影 */
  if (!_slider->is_confirmed)
  {
    char _value_str[10] = {};
    sprintf(_value_str, "%d", *_slider->value);
    int16_t _value_width = oled_get_UTF8_width(_value_str);
    int16_t _x_value = SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - _value_width + 2;
    oled_set_draw_color(1);
    oled_draw_str(_x_value + 2, _y + oled_get_str_height() / 2, _value_str);
  }
}

/**
 * @brief 绘制已确认滑块的数值覆盖层
 * @note  使用反色圆角矩形作为背景，使数值在 XOR 选择器上方清晰可读
 */
static void xerintosh_draw_slider_overlays(void)
{
  xerintosh_list_item_t *parent = g_xerintosh_selector.selected_item->parent;
  if (!parent) return;

  for (uint8_t i = 0; i < parent->child_num; i++)
  {
    xerintosh_list_item_t *_item = parent->child_list_item[i];
    if (_item->type != slider_item) continue;

    xerintosh_slider_item_t *_slider = xerintosh_to_slider_item(_item);
    if (!_slider->is_confirmed) continue;

    int16_t _y = _item->y_list_item + g_xerintosh_camera.y_camera - oled_get_str_height()/2;
    if (!is_item_visible(_y)) continue;

    char _value_str[10] = {};
    sprintf(_value_str, "%d", *_slider->value);
    int16_t _value_width = oled_get_UTF8_width(_value_str);
    int16_t _x_value = SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - _value_width + 2;

    /* 反色背景框 */
    oled_set_draw_color(0);
    oled_draw_R_box(_x_value, _y - 2, _value_width + 4, oled_get_str_height() - 2, 1);
    oled_set_draw_color(1);
    oled_draw_str(_x_value + 2, _y + oled_get_str_height() / 2, _value_str);
  }
}

/**
 * @brief 绘制用户自定义项（仅图标）
 */
static void draw_list_item_user(xerintosh_list_item_t *_item, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    xerintosh_draw_list_icon(_item->icon, _x, _y);
}

/* ═══ 列表项与文字绘制 ═══ */

/**
 * @brief 绘制当前可见的所有列表项（含图标、文字、滚动效果）
 */
void xerintosh_draw_list_item()
{
  for (unsigned char i = 0; i < g_xerintosh_selector.selected_item->parent->child_num; i++)
  {
    xerintosh_list_item_t *_item = g_xerintosh_selector.selected_item->parent->child_list_item[i];
    int16_t _x_list_item = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
    int16_t _y_list_item = _item->y_list_item + g_xerintosh_camera.y_camera - oled_get_str_height()/2;

    /* 根据类型分发到对应的绘制函数 */
    oled_set_draw_color(1);
    switch (_item->type)
    {
      case list_item:
        draw_list_item_list(_item, _x_list_item, _y_list_item);
        break;
      case switch_item:
        draw_list_item_switch(xerintosh_to_switch_item(_item), _x_list_item, _y_list_item);
        break;
      case button_item:
        draw_list_item_button(xerintosh_to_button_item(_item), _x_list_item, _y_list_item);
        break;
      case slider_item:
        draw_list_item_slider(xerintosh_to_slider_item(_item), _x_list_item, _y_list_item);
        break;
      case user_item:
        draw_list_item_user(_item, _x_list_item, _y_list_item);
        break;
      default:
        if (is_item_visible(_y_list_item))
          xerintosh_draw_list_icon(_item->icon, _x_list_item, _y_list_item);
        break;
    }

    /* ═══ 文字滚动效果 ═══ */
    xerintosh_set_font(hal_get_cn_font());
    if (_y_list_item + oled_get_str_height() / 2 > LIST_INFO_BAR_HEIGHT &&
        _y_list_item + oled_get_str_height() / 2 < SCREEN_HEIGHT)
    {
      int16_t _text_width = oled_get_UTF8_width(_item->content);
      int16_t _avail_width = SCREEN_WIDTH - LIST_ITEM_LEFT_MARGIN - 10 - LIST_ITEM_RIGHT_MARGIN;

      /* switch/slider 占用右侧空间，减小可用宽度 */
      if (_item->type == switch_item)
        _avail_width -= 11;
      else if (_item->type == slider_item)
        _avail_width -= 20;

      bool _is_selected = (_item == g_xerintosh_selector.selected_item);
      float _scroll_x = 0.0f;

      /* 计算文字循环滚动偏移 */
      if (_is_selected && _text_width > _avail_width) {
        if (!_item->is_scrolling) {
          _item->is_scrolling = true;
          _item->scroll_start_time = get_ticks();
        }
        uint32_t _elapsed = get_ticks() - _item->scroll_start_time;
        _scroll_x = xerintosh_compute_scroll_offset(_text_width, _avail_width, true, _elapsed);
      } else {
        _item->is_scrolling = false;
      }

      /* 设置裁剪区域：限制文字只在 icon 右侧到控件左侧之间显示 */
      int16_t _clip_x = LIST_ITEM_LEFT_MARGIN + 10;
      int16_t _clip_y = _y_list_item - oled_get_str_height() / 2 - 2;
      int16_t _clip_h = oled_get_str_height() + 4;
      hal_set_clip_rect(_clip_x, _clip_y, _avail_width, _clip_h);

      int16_t _cycle_dist = _text_width + _avail_width;
      int16_t _draw_x = _clip_x - (int16_t)_scroll_x;

      /* 绘制两份相同文字，形成无缝循环跑马灯 */
      oled_draw_UTF8(_draw_x,
                     _y_list_item + oled_get_str_height() / 2,
                     _item->content);
      oled_draw_UTF8(_draw_x + _cycle_dist,
                     _y_list_item + oled_get_str_height() / 2,
                     _item->content);

      hal_clear_clip_rect();
      /* ═══════════════════════ */
    }
  }

  g_xerintosh_refresh_list_value = false;
}

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
        oled_draw_H_line(2 + x, y - 2, 4);
        oled_draw_H_line(2 + x, y, 5);
        oled_draw_H_line(2 + x, y + 2, 3);
      break;
    case (switch_icon):
        oled_draw_circle(4 + x, y + 1, 3);
        oled_draw_V_line(4 + x, y, 3);
      break;
    case (plus_icon):
        oled_draw_circle(4 + x, y + 1, 3);
        oled_draw_V_line(4 + x, y, 3);
        oled_draw_H_line(3 + x, y + 1, 3);
      break;
    case (slider_icon):
        oled_draw_V_line(3 + x, y - 1, 5);
        oled_draw_V_line(6 + x, y - 1, 5);
        oled_draw_box(2 + x, y - 2, 3, 3);
        oled_draw_box(5 + x, y + 2, 3, 3);
      break;
    case (user_icon):
        oled_draw_str(2 + x, y + oled_get_str_height() / 2, "-");
      break;
    case (flag_icon):
        oled_draw_V_line(6 + x, y - 1, 5);
        oled_draw_box(3 + x, y - 2, 4, 3);
      break;
    case (power_icon):
        oled_draw_circle(4 + x, y + 1, 3);
        oled_draw_V_line(4 + x, y - 2, 3);
        oled_set_draw_color(0);
        oled_draw_pixel(x+3, y-2);
        oled_draw_pixel(x+5, y-2);
        oled_set_draw_color(1);
      break;
    default:
      break;
  }
}

/* ═══ 选择器 ═══ */

/**
 * @brief 绘制选择器高亮框（XOR 反色矩形 + 右侧虚线装饰）
 */
void xerintosh_draw_selector()
{
  int16_t _x_selector = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
  int16_t _y_selector = g_xerintosh_selector.y_selector + g_xerintosh_camera.y_camera;

  /* XOR 反色矩形 */
  hal_draw_xor_rect(_x_selector, _y_selector, g_xerintosh_selector.w_selector, g_xerintosh_selector.h_selector);

  /* 右侧虚线装饰 */
  oled_set_draw_color(1);
  for (int16_t i = g_xerintosh_selector.w_selector + _x_selector;
       i <= g_xerintosh_selector.w_selector + _x_selector + 7; i += 2)
  {
    for (int16_t j = _y_selector;
         j <= _y_selector + g_xerintosh_selector.h_selector - 1; j++)
    {
      if (j % 2 == 0)
        oled_draw_pixel(i + 1, j);
      if (j % 2 == 1)
        oled_draw_pixel(i, j);
    }
  }
}

/* ═══ 控件与长按提示 ═══ */

/**
 * @brief 绘制所有控件（信息栏 + 弹窗）
 */
void xerintosh_draw_widget()
{
  xerintosh_draw_info_bar();
  xerintosh_draw_pop_up();
}

/**
 * @brief 绘制长按进度提示条
 * @param duration_ms  当前已长按的毫秒数
 * @param threshold_ms 触发长按所需的毫秒数
 */
void xerintosh_draw_long_press_hint(uint32_t duration_ms, uint32_t threshold_ms)
{
    if (duration_ms == 0 || duration_ms >= threshold_ms) return;

    float progress = (float)duration_ms / (float)threshold_ms;
    uint16_t bar_width = 40;
    uint16_t bar_height = 4;
    uint16_t x = SCREEN_WIDTH - bar_width - 6;
    uint16_t y = SCREEN_HEIGHT - 6;

    oled_set_draw_color(1);
    oled_draw_frame(x, y, bar_width, bar_height);
    oled_draw_box(x + 1, y + 1, (uint16_t)((bar_width - 2) * progress), bar_height - 2);
}

/* ═══ 完整列表帧 ═══ */

/**
 * @brief 绘制完整列表帧（外观 + 列表项 + 选择器 + 滑块覆盖层）
 */
void xerintosh_draw_list()
{
  xerintosh_draw_list_appearance();
  xerintosh_draw_list_item();
  xerintosh_draw_selector();
  xerintosh_draw_slider_overlays();
}

/* ═══ 文字滚动工具 ═══ */

/**
 * @brief  计算文字循环滚动偏移量（纯函数，便于单元测试）
 * @param  text_width   文字总宽度（像素）
 * @param  avail_width  可用显示宽度（像素）
 * @param  is_selected  当前项是否被选中（仅选中项滚动）
 * @param  elapsed_ms   从选中开始经过的毫秒数
 * @return 水平偏移量（正值表示向左滚动）；无需滚动时返回 0
 */
float xerintosh_compute_scroll_offset(int16_t text_width, int16_t avail_width,
                                   bool is_selected, uint32_t elapsed_ms)
{
  if (!is_selected || text_width <= avail_width)
    return 0.0f;

  int16_t cycle_distance = text_width + avail_width;
  if (cycle_distance <= 0)
    return 0.0f;

  const uint32_t SCROLL_CYCLE_MS = 3000;
  uint32_t phase = elapsed_ms % SCROLL_CYCLE_MS;
  return ((float)phase / (float)SCROLL_CYCLE_MS) * (float)cycle_distance;
}
