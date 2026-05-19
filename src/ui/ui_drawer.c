#include "ui_drawer.h"

#include <math.h>
#include <stdio.h>

#include "ui_core.h"

uint8_t astra_exit_animation_status = 0;

void astra_draw_exit_animation()
{
  static float _temp_h = -8;
  static float _temp_h_trg = SCREEN_HEIGHT + 8;

  oled_set_draw_color(0);
  oled_draw_box(0, 0, SCREEN_WIDTH, _temp_h);
  oled_set_draw_color(1);

  uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
  int8_t _y_hourglass = _temp_h - SCREEN_HEIGHT / 2 - 18;
  if (_y_hourglass + 20 >= 0)
  {
    oled_draw_box(_x_hourglass_offset, _y_hourglass + 2, 13, 3);
    oled_set_draw_color(0);
    oled_draw_H_line(_x_hourglass_offset + 2, _y_hourglass + 3, 9);
    oled_set_draw_color(1);

    oled_draw_V_line(_x_hourglass_offset + 1, _y_hourglass + 4, 5);
    oled_draw_V_line(_x_hourglass_offset + 11, _y_hourglass + 4, 5);

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

    oled_draw_box(_x_hourglass_offset, _y_hourglass + 19, 13, 3);
    oled_set_draw_color(0);
    oled_draw_H_line(_x_hourglass_offset + 2, _y_hourglass + 20, 9);
    oled_set_draw_color(1);

    const uint8_t _points[][2] = {
      {5, 7}, {7, 7}, {6, 8}, {6, 10}, {6, 14}, {6, 16},
      {5, 17}, {7, 17}, {4, 18}, {6, 18}, {8, 18}
    };
    for (uint8_t i = 0; i < sizeof(_points) / sizeof(_points[0]); ++i)
      oled_draw_pixel(_x_hourglass_offset + _points[i][0], _y_hourglass + _points[i][1]);
  }

  if (_temp_h + 3 >= 0)
    for (uint8_t i = 0; i <= 3; ++i)
      oled_draw_H_line(0, _temp_h + i, SCREEN_WIDTH);

  for (int16_t i = 0; i <= SCREEN_WIDTH; i += 2)
    for (int16_t j = _temp_h - 5; j <= _temp_h - 1; j++)
    {
      if (j % 2 == 0)
        oled_draw_pixel(i + 1, j);
      if (j % 2 == 1)
        oled_draw_pixel(i, j);
    }

  astra_animation(&_temp_h, _temp_h_trg, ANIM_SPEED_EXIT);

  if (astra_exit_animation_status == 0 && _temp_h == _temp_h_trg && _temp_h == SCREEN_HEIGHT + 8)
  {
    astra_exit_animation_status = 1;
    return;
  }

  if (astra_exit_animation_status == 1)
  {
    _temp_h_trg = -8;
    astra_exit_animation_status = 2;
    return;
  }

  if (astra_exit_animation_status == 2 && _temp_h == _temp_h_trg && _temp_h == -8)
  {
    astra_exit_animation_finished = true;
    astra_exit_animation_status = 0;
    _temp_h = -8;
    _temp_h_trg = SCREEN_HEIGHT + 8;
    return;
  }
}

void astra_draw_info_bar()
{
  if (!astra_info_bar.is_running) return;

  if (astra_info_bar.y_info_bar == astra_info_bar.y_info_bar_trg)
    astra_info_bar.time = get_ticks();

  if (astra_info_bar.time - astra_info_bar.time_start >= astra_info_bar.span)
  {
    astra_info_bar.y_info_bar_trg = 0 - 2 * INFO_BAR_HEIGHT;
    if (astra_info_bar.y_info_bar == astra_info_bar.y_info_bar_trg)
      astra_info_bar.is_running = false;
  }

  int16_t _x_info_bar = SCREEN_WIDTH/2 - astra_info_bar.w_info_bar/2;
  int16_t _y_info_bar_1 = astra_info_bar.y_info_bar - 4;
  int16_t _y_info_bar_2 = astra_info_bar.y_info_bar + INFO_BAR_HEIGHT;

  astra_set_font(NULL);
  oled_set_draw_color(1);
  oled_draw_R_box(_x_info_bar + 3, _y_info_bar_1 + 3,
                  (int16_t)astra_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 4);

  oled_set_draw_color(0);
  oled_draw_R_box((int16_t)(SCREEN_WIDTH/2 - (astra_info_bar.w_info_bar + 4)/2), _y_info_bar_1,
                  (int16_t)(astra_info_bar.w_info_bar + 4), INFO_BAR_HEIGHT + 6, 4);

  oled_set_draw_color(1);
  oled_draw_R_box(_x_info_bar, _y_info_bar_1,
                  (int16_t)astra_info_bar.w_info_bar, INFO_BAR_HEIGHT + 4, 3);

  oled_set_draw_color(0);
  oled_draw_H_line(_x_info_bar + 2, _y_info_bar_2 - 2, (int16_t)(astra_info_bar.w_info_bar - 4));
  oled_draw_pixel(_x_info_bar + 1, _y_info_bar_2 - 3);
  oled_draw_pixel(_x_info_bar - 2, _y_info_bar_2 - 3);

  oled_draw_UTF8(_x_info_bar + 6,
                 (int16_t)(astra_info_bar.y_info_bar + oled_get_str_height() - 2),
                 astra_info_bar.content);
}

void astra_draw_pop_up()
{
  if (!astra_pop_up.is_running) return;

  if (astra_pop_up.y_pop_up == astra_pop_up.y_pop_up_trg)
    astra_pop_up.time = get_ticks();

  if (astra_pop_up.time - astra_pop_up.time_start >= astra_pop_up.span)
  {
    astra_pop_up.y_pop_up_trg = 0 - 2 * INFO_BAR_HEIGHT;
    if (astra_pop_up.y_pop_up == astra_pop_up.y_pop_up_trg)
      astra_pop_up.is_running = false;
  }

  int16_t _x_pop_up = SCREEN_WIDTH/2 - astra_pop_up.w_pop_up/2;
  int16_t _y_pop_up = astra_pop_up.y_pop_up + POP_UP_HEIGHT;

  astra_set_font(NULL);
  oled_set_draw_color(1);
  oled_draw_R_box(_x_pop_up + 1, (int16_t)astra_pop_up.y_pop_up + 3,
                  (int16_t)(astra_pop_up.w_pop_up + 4), POP_UP_HEIGHT, 4);

  oled_set_draw_color(0);
  oled_draw_R_box((int16_t)(SCREEN_WIDTH/2 - (astra_pop_up.w_pop_up + 4)/2 - 2), (int16_t)(astra_pop_up.y_pop_up - 2),
                  (int16_t)(astra_pop_up.w_pop_up + 8), POP_UP_HEIGHT + 4, 5);

  oled_set_draw_color(1);
  oled_draw_R_box(_x_pop_up - 2, (int16_t)astra_pop_up.y_pop_up,
                  (int16_t)(astra_pop_up.w_pop_up + 4), POP_UP_HEIGHT, 3);

  oled_set_draw_color(0);
  oled_draw_H_line(_x_pop_up, _y_pop_up - 2, (int16_t)astra_pop_up.w_pop_up);
  oled_draw_pixel(_x_pop_up - 1, _y_pop_up - 3);
  oled_draw_pixel((int16_t)(SCREEN_WIDTH/2 + astra_pop_up.w_pop_up/2), _y_pop_up - 3);

  oled_draw_UTF8(_x_pop_up + 3,
                 (int16_t)(astra_pop_up.y_pop_up + oled_get_str_height() + 1),
                 astra_pop_up.content);
}

void astra_draw_list_appearance()
{
  oled_set_draw_color(1);
  oled_draw_H_line(0, 1, 66);
  oled_draw_H_line(0, 0, 67);

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

  oled_draw_V_line(SCREEN_WIDTH - 5, 0, SCREEN_HEIGHT);
  oled_draw_V_line(SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT);

  static float _length_each_part = 0;
  _length_each_part = ceilf((SCREEN_HEIGHT - 10.0f) / (float) astra_selector.selected_item->parent->child_num);
  oled_draw_box(SCREEN_WIDTH - 4, 5 + astra_selector.selected_index * _length_each_part, 3, _length_each_part);

  oled_set_draw_color(0);
  oled_draw_H_line(SCREEN_WIDTH - 4, _length_each_part + (float)astra_selector.selected_index * _length_each_part, 3);

  if (_length_each_part >= 9)
  {
    oled_draw_H_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part - 2.0f + (float)astra_selector.selected_index * _length_each_part), 3);
    oled_draw_H_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part + 2.0f + (float)astra_selector.selected_index * _length_each_part), 3);
  }

  oled_set_draw_color(1);
  oled_draw_box(SCREEN_WIDTH - 4, 0, 3, 4);
  oled_draw_box(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, 3, 4);
  oled_set_draw_color(0);
  oled_draw_H_line(SCREEN_WIDTH - 4, 2, 3);
  oled_draw_pixel(SCREEN_WIDTH - 3, 1);
  oled_draw_H_line(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 3, 3);
  oled_draw_pixel(SCREEN_WIDTH - 3, SCREEN_HEIGHT - 2);
}

static bool is_item_visible(int16_t _y_item)
{
  return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}

static void draw_list_item_list(astra_list_item_t *_item, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    astra_draw_list_icon(_item->icon, _x, _y);
}

static void draw_list_item_switch(astra_switch_item_t *_switch, int16_t _x, int16_t _y)
{
  if (_switch->init_function && astra_refresh_list_value)
    _switch->init_function();
  if (!is_item_visible(_y)) return;

  astra_draw_list_icon(_switch->base_item.icon, _x, _y);
  oled_draw_frame(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 7, _y - 2, 11, 7);
  if (*_switch->value)
  {
    oled_draw_box(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 1, _y, 3, 3);
    oled_draw_pixel(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 4, _y + 1);
  }
  else
  {
    oled_draw_box(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 5, _y, 3, 3);
    oled_draw_pixel(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN, _y + 1);
  }
}

static void draw_list_item_button(astra_button_item_t *_button, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    astra_draw_list_icon(_button->base_item.icon, _x, _y);
}

static void draw_list_item_slider(astra_slider_item_t *_slider, int16_t _x, int16_t _y)
{
  if (_slider->init_function && astra_refresh_list_value)
    _slider->init_function();
  if (!is_item_visible(_y)) return;

  astra_draw_list_icon(_slider->base_item.icon, _x, _y);
  char _value_str[10] = {};
  sprintf(_value_str, "%d", *_slider->value);
  int16_t _x_value = SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - oled_get_str_width(_value_str) + 2;

  if (_slider->is_confirmed)
  {
    static uint32_t _last_tick = 0;
    static bool _is_visiable = false;
    uint32_t _ticks = get_ticks();
    if (_is_visiable)
    {
      oled_set_draw_color(1);
      oled_draw_R_box(_x_value, _y - 4, oled_get_UTF8_width(_value_str) + 4, oled_get_str_height() - 2, 1);
    }
    oled_set_draw_color(0);
    oled_draw_str(_x_value + 2, _y + oled_get_str_height() / 2, _value_str);
    if (_ticks - _last_tick >= 1000)
    {
      _is_visiable = !_is_visiable;
      _last_tick = _ticks;
    }
  }
  else
  {
    oled_draw_str(_x_value + 2, _y + oled_get_str_height() / 2, _value_str);
  }
}

static void draw_list_item_user(astra_list_item_t *_item, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    astra_draw_list_icon(_item->icon, _x, _y);
}

void astra_draw_list_item()
{
  for (unsigned char i = 0; i < astra_selector.selected_item->parent->child_num; i++)
  {
    astra_list_item_t *_item = astra_selector.selected_item->parent->child_list_item[i];
    int16_t _x_list_item = astra_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
    int16_t _y_list_item = _item->y_list_item + astra_camera.y_camera - oled_get_str_height()/2;

    oled_set_draw_color(1);
    switch (_item->type)
    {
      case list_item:
        draw_list_item_list(_item, _x_list_item, _y_list_item);
        break;
      case switch_item:
        draw_list_item_switch(astra_to_switch_item(_item), _x_list_item, _y_list_item);
        break;
      case button_item:
        draw_list_item_button(astra_to_button_item(_item), _x_list_item, _y_list_item);
        break;
      case slider_item:
        draw_list_item_slider(astra_to_slider_item(_item), _x_list_item, _y_list_item);
        break;
      case user_item:
        draw_list_item_user(_item, _x_list_item, _y_list_item);
        break;
      default:
        if (is_item_visible(_y_list_item))
          astra_draw_list_icon(_item->icon, _x_list_item, _y_list_item);
        break;
    }

    astra_set_font(NULL);
    if (_y_list_item + oled_get_str_height() / 2 > LIST_INFO_BAR_HEIGHT &&
        _y_list_item + oled_get_str_height() / 2 < SCREEN_HEIGHT)
      oled_draw_UTF8(10 + _x_list_item, _y_list_item + oled_get_str_height() / 2, _item->content);
  }

  astra_refresh_list_value = false;
}

void astra_draw_list_icon(astra_list_item_icon_t icon, uint16_t x, uint16_t y){
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

void astra_draw_selector()
{
  int16_t _x_selector = astra_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
  int16_t _y_selector = astra_selector.y_selector + astra_camera.y_camera;

  hal_draw_xor_rect(_x_selector, _y_selector, astra_selector.w_selector, astra_selector.h_selector);

  oled_set_draw_color(1);
  for (int16_t i = astra_selector.w_selector + _x_selector;
       i <= astra_selector.w_selector + _x_selector + 7; i += 2)
  {
    for (int16_t j = _y_selector;
         j <= _y_selector + astra_selector.h_selector - 1; j++)
    {
      if (j % 2 == 0)
        oled_draw_pixel(i + 1, j);
      if (j % 2 == 1)
        oled_draw_pixel(i, j);
    }
  }
}

void astra_draw_widget()
{
  astra_draw_info_bar();
  astra_draw_pop_up();
}

void astra_draw_list()
{
  astra_draw_list_appearance();
  astra_draw_list_item();
  astra_draw_selector();
}
