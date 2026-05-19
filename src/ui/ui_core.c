#include "ui_core.h"
#include <stdio.h>
#include "ui_drawer.h"
#include <math.h>

bool in_astra = false;
uint16_t astra_draw_color = 0xFFFF;

/**
 * @brief 进入astra ui lite
 *
 * @note 需要运行在循环中
 * @note 可以通过按键等传感器进行触发 当in_astra为true时进入astra ui lite
 */
void ad_astra()
{
  /* Splash screen / long-press entry removed for TFT build */
}

bool astra_is_in_user_item()
{
  return (astra_selector.selected_item->type == user_item && astra_to_user_item(astra_selector.selected_item)->in_user_item) ? true : false;
}

void astra_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (_speed >= 99.0f) _speed = 99.0f;
    if (fabs(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
    else *_pos += (_pos_trg - *_pos) / (100.0f - _speed) / 1.0f;
  }
}

void astra_refresh_info_bar()
{
  astra_animation(&astra_info_bar.y_info_bar, astra_info_bar.y_info_bar_trg, ANIM_SPEED_INFO_BAR);
  astra_animation(&astra_info_bar.w_info_bar, astra_info_bar.w_info_bar_trg, ANIM_SPEED_INFO_BAR_W);
}

void astra_refresh_pop_up()
{
  astra_animation(&astra_pop_up.y_pop_up, astra_pop_up.y_pop_up_trg, ANIM_SPEED_POP_UP_Y);
  astra_animation(&astra_pop_up.w_pop_up, astra_pop_up.w_pop_up_trg, ANIM_SPEED_POP_UP_W);
}

void astra_refresh_camera_position()
{
  //15为selector的高度
  if (astra_camera.selector->y_selector_trg + 15 + astra_camera.y_camera_trg > SCREEN_HEIGHT)  //向下超出屏幕 需要向下移动
    astra_camera.y_camera_trg = SCREEN_HEIGHT - astra_camera.selector->y_selector_trg - 15;

  if (astra_camera.selector->y_selector_trg + astra_camera.y_camera_trg < 0)  //向上超出屏幕 需要向上移动
    astra_camera.y_camera_trg = 0 - astra_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  astra_animation(&astra_camera.x_camera, astra_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  astra_animation(&astra_camera.y_camera, astra_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}

void astra_refresh_widget_core_position()
{
  //需要调用所有的widget refresh函数
  astra_refresh_info_bar();
  astra_refresh_pop_up();
}

void astra_init_list()
{
  //做动画
  for (uint8_t i = 0; i < astra_get_root_list()->child_num; i++)
    astra_get_root_list()->child_list_item[i]->y_list_item = 0;
  astra_selector.selected_index = 0;
  astra_selector.selected_item = astra_get_root_list()->child_list_item[0];
  astra_selector.y_selector = SCREEN_HEIGHT;
  astra_selector.h_selector = SCREEN_HEIGHT;
}

void astra_init_core()
{
  astra_init_list();
  astra_list_item_t *root = astra_get_root_list();
  if (root->child_num > 0)
    astra_bind_item_to_selector(root->child_list_item[0]);
  else
    astra_bind_item_to_selector(root);
  astra_bind_selector_to_camera(astra_get_selector());
}

void astra_refresh_list_item_position()
{
  for (uint8_t i = 0; i < astra_selector.selected_item->parent->child_num; i++)
    astra_animation(&astra_selector.selected_item->parent->child_list_item[i]->y_list_item, astra_selector.selected_item->parent->child_list_item[i]->y_list_item_trg, ANIM_SPEED_LIST_ITEM);
}

void astra_refresh_selector_position()
{
  astra_set_font(hal_get_cn_font());
  astra_selector.y_selector_trg = astra_selector.selected_item->y_list_item_trg - oled_get_str_height() + 1;
  if (astra_selector.selected_item->type == switch_item || astra_selector.selected_item->type == slider_item)
    astra_selector.w_selector_trg = SCREEN_WIDTH - 18;
  else astra_selector.w_selector_trg = oled_get_UTF8_width(astra_selector.selected_item->content) + 12;
  astra_selector.h_selector_trg = oled_get_str_height() + 4;
  astra_animation(&astra_selector.y_selector, astra_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
  astra_animation(&astra_selector.w_selector, astra_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
  astra_animation(&astra_selector.h_selector, astra_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
}

void astra_refresh_main_core_position()
{
  astra_refresh_list_item_position();
}

void astra_ui_widget_core()
{
  astra_refresh_widget_core_position();
  astra_draw_widget();
}

void astra_ui_main_core()
{
  if (!in_astra) return;

  //切换in user item的逻辑
  if (astra_selector.selected_item->type == user_item && !astra_to_user_item(astra_selector.selected_item)->in_user_item)
  {
    astra_user_item_t *_selected_user_item = astra_to_user_item(astra_selector.selected_item);

    if (_selected_user_item->entering_user_item && astra_exit_animation_status == 1)
    {
      if (_selected_user_item->init_function != NULL)
        _selected_user_item->init_function();
      _selected_user_item->in_user_item = 1;
    }
  }

  //渲染的逻辑
  if (astra_selector.selected_item->type == user_item && astra_to_user_item(astra_selector.selected_item)->in_user_item)
  {
    astra_user_item_t* _selected_user_item = astra_to_user_item(astra_selector.selected_item);

    if (_selected_user_item->loop_function != NULL)
    {
      _selected_user_item->loop_function();
    }

    if (_selected_user_item->exiting_user_item && astra_exit_animation_status == 1)
    {
        if (_selected_user_item->exit_function != NULL)
            _selected_user_item->exit_function();
        _selected_user_item->in_user_item = 0;
    }
  } else
  {
    astra_refresh_camera_position();
    astra_refresh_main_core_position();
    astra_refresh_selector_position();
    astra_draw_list();
  }

  //退场动画
  //上面都是正常应当绘制的内容 退场动画需要绘制时 只需要在上面的基础上绘制遮罩即可
  if (!astra_exit_animation_finished)
    astra_draw_exit_animation();
}
