/**
 * @file   ui_draw_list.c
 * @brief  UI 列表绘制实现
 * @details 实现列表外观、列表项、选择器高亮、滑块覆盖层、长按提示及文字滚动。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_drawer.h"
#include "ui_item.h"
#include "ui_core.h"
#include "ui_dirty.h"
#include "hal/hal_system.h"
#include <math.h>

/* ═══ 可见性判断 ═══ */

/**
 * @brief 判断列表项是否在屏幕可视区域内
 * @param _y_item 项的 y 坐标
 * @return true  可见
 * @return false 不可见
 */
bool xerintosh_is_item_visible(int16_t _y_item)
{
  return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}

/**
 * @brief 绘制已确认滑块的数值覆盖层
 * @note  遍历当前父项的所有子项，通过派发表绘制 slider 的覆盖层。
 */
static void xerintosh_draw_slider_overlays(void)
{
  xerintosh_list_item_t *sel = g_xerintosh_selector.selected_item;
  if (sel == NULL || sel->parent == NULL) return;

  xerintosh_list_item_t *parent = sel->parent;
  for (uint8_t i = 0; i < parent->child_num; i++)
  {
    xerintosh_dispatch_draw_overlay(parent->child_list_item[i]);
  }
}

/* ═══ 列表外观 ═══ */

/**
 * @brief 绘制列表整体外观（边框、滚动条、装饰像素）
 * @note  顶部装饰横线 + 右侧滚动指示条（显示当前选中位置比例）
 */
static void xerintosh_draw_list_appearance(int16_t selected_index, int16_t child_num)
{
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_h_line(0, 1, 66, g_xerintosh_draw_color);
  hal_draw_h_line(0, 0, 67, g_xerintosh_draw_color);

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
      hal_draw_pixel(i, draw_cfg[j]._y, g_xerintosh_draw_color);

  /* 右侧边框竖线 */
  hal_draw_v_line(SCREEN_WIDTH - 5, 0, SCREEN_HEIGHT, g_xerintosh_draw_color);
  hal_draw_v_line(SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT, g_xerintosh_draw_color);

  /* 滚动条 */
  static uint8_t _cached_child_num = 0;
  static float _cached_length = 0;
  if (_cached_child_num != child_num) {
    _cached_child_num = child_num;
    _cached_length = ceilf((SCREEN_HEIGHT - 10.0f) / (float)_cached_child_num);
  }
  float _length_each_part = _cached_length;
  hal_draw_fill_rect(SCREEN_WIDTH - 4, 5 + selected_index * _length_each_part, 3, _length_each_part, g_xerintosh_draw_color);

  /* 滚动条内部高光线 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(SCREEN_WIDTH - 4, _length_each_part + (float)selected_index * _length_each_part, 3, g_xerintosh_draw_color);

  if (_length_each_part >= 9)
  {
    hal_draw_h_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part - 2.0f + (float)selected_index * _length_each_part), 3, g_xerintosh_draw_color);
    hal_draw_h_line(SCREEN_WIDTH - 4,
                     floorf(_length_each_part + 2.0f + (float)selected_index * _length_each_part), 3, g_xerintosh_draw_color);
  }

  /* 滚动条上下端点 */
  g_xerintosh_draw_color = COLOR_FG;
  hal_draw_fill_rect(SCREEN_WIDTH - 4, 0, 3, 4, g_xerintosh_draw_color);
  hal_draw_fill_rect(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, 3, 4, g_xerintosh_draw_color);
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_h_line(SCREEN_WIDTH - 4, 2, 3, g_xerintosh_draw_color);
  hal_draw_pixel(SCREEN_WIDTH - 3, 1, g_xerintosh_draw_color);
  hal_draw_h_line(SCREEN_WIDTH - 4, SCREEN_HEIGHT - 3, 3, g_xerintosh_draw_color);
  hal_draw_pixel(SCREEN_WIDTH - 3, SCREEN_HEIGHT - 2, g_xerintosh_draw_color);
}

/* ═══ 列表项与文字绘制 ═══ */

/**
 * @brief 绘制当前可见的所有列表项（含图标、文字、滚动效果）
 */
static void xerintosh_draw_list_item()
{
  xerintosh_set_font(hal_get_cn_font());
  int16_t _font_h = hal_get_font_height();
  int16_t _font_h_2 = _font_h / 2;

  for (unsigned char i = 0; i < g_xerintosh_selector.selected_item->parent->child_num; i++)
  {
    xerintosh_list_item_t *_item = g_xerintosh_selector.selected_item->parent->child_list_item[i];
    int16_t _x_list_item = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
    int16_t _y_list_item = _item->y_list_item + g_xerintosh_camera.y_camera - _font_h_2;

    /* 根据类型分发到对应的绘制函数 */
    g_xerintosh_draw_color = COLOR_FG;
    xerintosh_dispatch_draw(_item, _x_list_item, _y_list_item);

    /* 自定义位图图标补充绘制 */
    if (_item->icon == custom_icon && _item->bitmap_data != NULL) {
      xerintosh_draw_item_bitmap(_item, _x_list_item, _y_list_item);
    }

    /* ═══ 文字绘制与滚动效果 ═══ */
    if (_y_list_item + _font_h_2 > LIST_INFO_BAR_HEIGHT &&
        _y_list_item + _font_h_2 < SCREEN_HEIGHT)
    {
      int16_t _text_width = hal_get_string_width(_item->content);
      bool _has_right_control = xerintosh_dispatch_has_right_control(_item);
      int16_t _right_margin = _has_right_control ? LIST_ITEM_RIGHT_MARGIN : 4;
      int16_t _avail_width = SCREEN_WIDTH - LIST_ITEM_LEFT_MARGIN - 10 - _right_margin;

      /* switch/slider 额外占用右侧控件空间 */
      if (_has_right_control)
        _avail_width -= 11;

      bool _is_selected = (_item == g_xerintosh_selector.selected_item);
      float _scroll_x = 0.0f;

      /* 计算文字循环滚动偏移 */
      if (_is_selected && _text_width > _avail_width) {
        if (!_item->is_scrolling) {
          _item->is_scrolling = true;
          _item->scroll_start_time = hal_get_ticks();
        }
        uint32_t _elapsed = hal_get_ticks() - _item->scroll_start_time;
        _scroll_x = xerintosh_compute_scroll_offset(_text_width, _avail_width, true, _elapsed);

        /* 文字滚动期间每帧都需清屏重绘，否则旧像素残留造成残影 */
        xerintosh_invalidate();
      } else {
        _item->is_scrolling = false;
      }

      /* 设置裁剪区域：限制文字只在 icon 右侧到控件左侧之间显示 */
      int16_t _clip_x = LIST_ITEM_LEFT_MARGIN + 10;
      int16_t _clip_y = _y_list_item - _font_h_2 - 2;
      int16_t _clip_h = _font_h + 4;
      hal_set_clip_rect(_clip_x, _clip_y, _avail_width, _clip_h);

      int16_t _cycle_dist = _text_width + _avail_width;
      int16_t _draw_x = _clip_x - (int16_t)_scroll_x;

      /* 绘制两份相同文字，形成无缝循环跑马灯 */
      hal_draw_string(_draw_x,
                     _y_list_item + _font_h_2,
                     _item->content, g_xerintosh_draw_color);
      hal_draw_string(_draw_x + _cycle_dist,
                     _y_list_item + _font_h_2,
                     _item->content, g_xerintosh_draw_color);

      hal_clear_clip_rect();
      /* ═══════════════════════ */
    }
  }

  g_xerintosh_refresh_list_value = false;
}

/* ═══ 选择器 ═══ */

/**
 * @brief 绘制选择器高亮框（XOR 反色矩形 + 右侧虚线装饰）
 */
static void xerintosh_draw_selector()
{
  int16_t _x_selector = g_xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
  int16_t _y_selector = g_xerintosh_selector.y_selector + g_xerintosh_camera.y_camera;

  /* XOR 反色矩形 */
  hal_draw_xor_rect(_x_selector, _y_selector, g_xerintosh_selector.w_selector, g_xerintosh_selector.h_selector);

  /* 右侧虚线装饰 */
  g_xerintosh_draw_color = COLOR_FG;
  for (int16_t i = g_xerintosh_selector.w_selector + _x_selector;
       i <= g_xerintosh_selector.w_selector + _x_selector + 7; i += 2)
  {
    for (int16_t j = _y_selector;
         j <= _y_selector + g_xerintosh_selector.h_selector - 1; j++)
    {
      if (j % 2 == 0)
        hal_draw_pixel(i + 1, j, g_xerintosh_draw_color);
      if (j % 2 == 1)
        hal_draw_pixel(i, j, g_xerintosh_draw_color);
    }
  }
}

/* ═══ 长按提示 ═══ */

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

    g_xerintosh_draw_color = COLOR_FG;
    hal_draw_rect(x, y, bar_width, bar_height, g_xerintosh_draw_color);
    hal_draw_fill_rect(x + 1, y + 1, (uint16_t)((bar_width - 2) * progress), bar_height - 2, g_xerintosh_draw_color);
}

/* ═══ 完整列表帧 ═══ */

/**
 * @brief 绘制完整列表帧（外观 + 列表项 + 选择器 + 滑块覆盖层）
 */
void xerintosh_draw_list()
{
  xerintosh_draw_list_appearance(g_xerintosh_selector.selected_index,
                                g_xerintosh_selector.selected_item->parent->child_num);
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
