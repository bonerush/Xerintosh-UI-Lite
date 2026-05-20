#include "ui_core.h"
#include <stdio.h>
#include "ui_drawer.h"
#include <math.h>

bool in_xerintosh = false;
uint16_t xerintosh_draw_color = 0xFFFF;
bool g_anim_enabled = true;

/**
 * @brief 进入Xerintosh ui lite
 *
 * @note 需要运行在循环中
 * @note 可以通过按键等传感器进行触发 当in_xerintosh为true时进入Xerintosh ui lite
 */
void ad_xerintosh()
{
  /* Splash screen / long-press entry removed for TFT build */
}

bool xerintosh_is_in_user_item()
{
  return (xerintosh_selector.selected_item->type == user_item && xerintosh_to_user_item(xerintosh_selector.selected_item)->in_user_item) ? true : false;
}

void xerintosh_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (!g_anim_enabled) {
      *_pos = _pos_trg;
      return;
    }
    if (_speed >= 99.0f) _speed = 99.0f;
    if (fabs(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
    else *_pos += (_pos_trg - *_pos) / (100.0f - _speed) / 1.0f;
  }
}

void xerintosh_refresh_info_bar()
{
  xerintosh_animation(&xerintosh_info_bar.y_info_bar, xerintosh_info_bar.y_info_bar_trg, ANIM_SPEED_INFO_BAR);
  xerintosh_animation(&xerintosh_info_bar.w_info_bar, xerintosh_info_bar.w_info_bar_trg, ANIM_SPEED_INFO_BAR_W);
}

void xerintosh_refresh_pop_up()
{
  xerintosh_animation(&xerintosh_pop_up.y_pop_up, xerintosh_pop_up.y_pop_up_trg, ANIM_SPEED_POP_UP_Y);
  xerintosh_animation(&xerintosh_pop_up.w_pop_up, xerintosh_pop_up.w_pop_up_trg, ANIM_SPEED_POP_UP_W);
}

void xerintosh_refresh_camera_position()
{
  //15为selector的高度
  if (xerintosh_camera.selector->y_selector_trg + 15 + xerintosh_camera.y_camera_trg > SCREEN_HEIGHT)  //向下超出屏幕 需要向下移动
    xerintosh_camera.y_camera_trg = SCREEN_HEIGHT - xerintosh_camera.selector->y_selector_trg - 15;

  if (xerintosh_camera.selector->y_selector_trg + xerintosh_camera.y_camera_trg < 0)  //向上超出屏幕 需要向上移动
    xerintosh_camera.y_camera_trg = 0 - xerintosh_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  xerintosh_animation(&xerintosh_camera.x_camera, xerintosh_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  xerintosh_animation(&xerintosh_camera.y_camera, xerintosh_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}

void xerintosh_refresh_widget_core_position()
{
  //需要调用所有的widget refresh函数
  xerintosh_refresh_info_bar();
  xerintosh_refresh_pop_up();
}

void xerintosh_init_list()
{
  //做动画
  for (uint8_t i = 0; i < xerintosh_get_root_list()->child_num; i++)
    xerintosh_get_root_list()->child_list_item[i]->y_list_item = 0;
  xerintosh_selector.selected_index = 0;
  xerintosh_selector.selected_item = xerintosh_get_root_list()->child_list_item[0];
  xerintosh_selector.y_selector = SCREEN_HEIGHT;
  xerintosh_selector.h_selector = SCREEN_HEIGHT;
}

void xerintosh_init_core()
{
  xerintosh_init_list();
  xerintosh_list_item_t *root = xerintosh_get_root_list();
  if (root->child_num > 0)
    xerintosh_bind_item_to_selector(root->child_list_item[0]);
  else
    xerintosh_bind_item_to_selector(root);
  xerintosh_bind_selector_to_camera(xerintosh_get_selector());
}

void xerintosh_refresh_list_item_position()
{
  for (uint8_t i = 0; i < xerintosh_selector.selected_item->parent->child_num; i++)
    xerintosh_animation(&xerintosh_selector.selected_item->parent->child_list_item[i]->y_list_item, xerintosh_selector.selected_item->parent->child_list_item[i]->y_list_item_trg, ANIM_SPEED_LIST_ITEM);
}

void xerintosh_refresh_selector_position()
{
  xerintosh_set_font(hal_get_cn_font());
  xerintosh_selector.y_selector_trg = xerintosh_selector.selected_item->y_list_item_trg - oled_get_str_height() + 1;
  if (xerintosh_selector.selected_item->type == switch_item || xerintosh_selector.selected_item->type == slider_item)
    xerintosh_selector.w_selector_trg = SCREEN_WIDTH - 18;
  else xerintosh_selector.w_selector_trg = oled_get_UTF8_width(xerintosh_selector.selected_item->content) + 12;
  xerintosh_selector.h_selector_trg = oled_get_str_height() + 4;
  xerintosh_animation(&xerintosh_selector.y_selector, xerintosh_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&xerintosh_selector.w_selector, xerintosh_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&xerintosh_selector.h_selector, xerintosh_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
}

void xerintosh_refresh_main_core_position()
{
  xerintosh_refresh_list_item_position();
}

void xerintosh_ui_widget_core()
{
  xerintosh_refresh_widget_core_position();
  xerintosh_draw_widget();
}

void xerintosh_ui_main_core()
{
  if (!in_xerintosh) return;

  //切换in user item的逻辑
  if (xerintosh_selector.selected_item->type == user_item && !xerintosh_to_user_item(xerintosh_selector.selected_item)->in_user_item)
  {
    xerintosh_user_item_t *_selected_user_item = xerintosh_to_user_item(xerintosh_selector.selected_item);

    if (_selected_user_item->entering_user_item && xerintosh_exit_animation_status == 1)
    {
      if (_selected_user_item->init_function != NULL)
        _selected_user_item->init_function();
      _selected_user_item->in_user_item = 1;
    }
  }

  //渲染的逻辑
  if (xerintosh_selector.selected_item->type == user_item && xerintosh_to_user_item(xerintosh_selector.selected_item)->in_user_item)
  {
    xerintosh_user_item_t* _selected_user_item = xerintosh_to_user_item(xerintosh_selector.selected_item);

    if (_selected_user_item->loop_function != NULL)
    {
      _selected_user_item->loop_function();
    }

    if (_selected_user_item->exiting_user_item && xerintosh_exit_animation_status == 1)
    {
        if (_selected_user_item->exit_function != NULL)
            _selected_user_item->exit_function();
        _selected_user_item->in_user_item = 0;
    }
  } else
  {
    xerintosh_refresh_camera_position();
    xerintosh_refresh_main_core_position();
    xerintosh_refresh_selector_position();
    xerintosh_draw_list();
  }

  //退场动画
  //上面都是正常应当绘制的内容 退场动画需要绘制时 只需要在上面的基础上绘制遮罩即可
  if (!xerintosh_exit_animation_finished)
    xerintosh_draw_exit_animation();
}
