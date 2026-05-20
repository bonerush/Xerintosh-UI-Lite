#include "ui_item.h"

#include <stdlib.h>
#include <string.h>

#include "ui_core.h"

void xerintosh_set_font(const void *_font)
{
  if (_font != xerintosh_font) oled_set_font(_font);
}

xerintosh_info_bar_t xerintosh_info_bar = {0, 1, 0 - 2 * INFO_BAR_HEIGHT, 0 - 2 * INFO_BAR_HEIGHT, 80, 80, false, 0, 1};

void xerintosh_push_info_bar(const char *_content, const uint16_t _span)
{
  //设定显示时间的概念，超过了显示时间，就将ytrg设为初始位置，如果在显示时间之内，有新的消息涌入，则y和ytrg都不变，继续显示，且显示时间清零
  //只有显示时间到了的时候，才会复位

  xerintosh_info_bar.time = get_ticks();
  xerintosh_info_bar.content = _content;
  xerintosh_info_bar.span = _span;
  xerintosh_info_bar.is_running = false; //每次进入该函数都代表有新的消息涌入，所以需要重置is_running

  //展开弹窗 收回弹窗和同步时间戳需要在循环中进行 所以移到了drawer中
  if (!xerintosh_info_bar.is_running)
  {
    xerintosh_info_bar.time_start = get_ticks();
    xerintosh_info_bar.y_info_bar_trg = 0;
    xerintosh_info_bar.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());
  xerintosh_info_bar.w_info_bar_trg = oled_get_UTF8_width(xerintosh_info_bar.content) + INFO_BAR_OFFSET;
}

xerintosh_pop_up_t xerintosh_pop_up = {0, 1, 0 - 2 * POP_UP_HEIGHT, 0 - 2 * POP_UP_HEIGHT, 80, 80, false, 0, 1};

void xerintosh_push_pop_up(const char *_content, const uint16_t _span)
{
  if (xerintosh_pop_up.is_running && xerintosh_pop_up.content != NULL && strcmp(xerintosh_pop_up.content, _content) == 0) {
    xerintosh_pop_up.time_start = get_ticks();
    xerintosh_pop_up.span = _span;
    xerintosh_pop_up.y_pop_up_trg = 20;
    return;
  }

  xerintosh_pop_up.time = get_ticks();
  xerintosh_pop_up.content = _content;
  xerintosh_pop_up.span = _span;
  xerintosh_pop_up.is_running = false;

  //弹出
  if (!xerintosh_pop_up.is_running)
  {
    xerintosh_pop_up.time_start = get_ticks();
    xerintosh_pop_up.y_pop_up_trg = 20;
    xerintosh_pop_up.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());
  xerintosh_pop_up.w_pop_up_trg = oled_get_UTF8_width(xerintosh_pop_up.content) + POP_UP_OFFSET;
}

void xerintosh_hide_pop_up(void)
{
  xerintosh_pop_up.is_running = false;
  xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
  xerintosh_pop_up.y_pop_up = 0 - 2 * POP_UP_HEIGHT;
}

// xerintosh_list_item_t xerintosh_list_item_root = {};

static xerintosh_list_item_t *xerintosh_safe_cast(xerintosh_list_item_t *_item, xerintosh_list_item_type_t _expected_type)
{
  if (_item != NULL && _item->type == _expected_type)
    return _item;
  return xerintosh_get_root_list();
}

xerintosh_switch_item_t *xerintosh_to_switch_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_switch_item_t*)xerintosh_safe_cast(_xerintosh_list_item, switch_item);
}

xerintosh_button_item_t *xerintosh_to_button_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_button_item_t*)xerintosh_safe_cast(_xerintosh_list_item, button_item);
}

xerintosh_slider_item_t *xerintosh_to_slider_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_slider_item_t*)xerintosh_safe_cast(_xerintosh_list_item, slider_item);
}

xerintosh_user_item_t *xerintosh_to_user_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_user_item_t*)xerintosh_safe_cast(_xerintosh_list_item, user_item);
}

static void xerintosh_init_base_item(xerintosh_list_item_t *_item, xerintosh_list_item_type_t _type,
                                  const char *_content, xerintosh_list_item_icon_t _icon,
                                  xerintosh_list_item_icon_t _default_icon)
{
  memset(_item, 0, sizeof(xerintosh_list_item_t));
  _item->type = _type;
  _item->content = _content ? strdup(_content) : NULL;
  _item->icon = (_icon == default_icon) ? _default_icon : _icon;
}

//tips: 不会重复创建root节点
xerintosh_list_item_t *xerintosh_get_root_list()
{
  static xerintosh_list_item_t *_xerintosh_list_root_item = NULL;
  if (_xerintosh_list_root_item == NULL)
  {
    _xerintosh_list_root_item = (xerintosh_list_item_t*)malloc(sizeof(xerintosh_list_item_t));
    if (_xerintosh_list_root_item == NULL) return NULL;
    xerintosh_init_base_item(_xerintosh_list_root_item, list_item, "root", default_icon, list_icon);
  }
  return _xerintosh_list_root_item;
}

xerintosh_list_item_t *xerintosh_new_list_item(const char *_content, xerintosh_list_item_icon_t icon)
{
  xerintosh_list_item_t *_item = (xerintosh_list_item_t*)malloc(sizeof(xerintosh_list_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(_item, list_item, _content, icon, list_icon);
  return _item;
}

xerintosh_list_item_t *xerintosh_new_switch_item(const char *_content, bool *_value,
                                          void (*_init_function)(), void (*_exit_function)(),
                                          xerintosh_list_item_icon_t icon)
{
  xerintosh_switch_item_t *_item = (xerintosh_switch_item_t*)malloc(sizeof(xerintosh_switch_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, switch_item, _content, icon, switch_icon);
  _item->value = _value;
  _item->init_function = _init_function;
  _item->exit_function = _exit_function;
  return (xerintosh_list_item_t*)_item;
}

xerintosh_list_item_t *xerintosh_new_button_item(const char *_content, void (*_exit_function)(),
                                          xerintosh_list_item_icon_t icon)
{
  xerintosh_button_item_t *_item = (xerintosh_button_item_t*)malloc(sizeof(xerintosh_button_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, button_item, _content, icon, plus_icon);
  _item->exit_function = _exit_function;
  return (xerintosh_list_item_t*)_item;
}

xerintosh_list_item_t *xerintosh_new_slider_item(const char *_content, int16_t *_value, uint8_t _step,
                                          int16_t _min, int16_t _max,
                                          void (*_init_function)(), void (*_exit_function)(),
                                          xerintosh_list_item_icon_t icon)
{
  xerintosh_slider_item_t *_item = (xerintosh_slider_item_t*)malloc(sizeof(xerintosh_slider_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, slider_item, _content, icon, slider_icon);
  _item->value = _value;
  _item->value_step = _step;
  _item->value_min = _min;
  _item->value_max = _max;
  _item->value_backup = *_value;
  _item->is_confirmed = false;
  _item->init_function = _init_function;
  _item->exit_function = _exit_function;
  return (xerintosh_list_item_t*)_item;
}

xerintosh_list_item_t *xerintosh_new_user_item(const char *_content, void (*_init_function)(),
                                        void (*_loop_function)(), void (*_exit_function)(),
                                        xerintosh_list_item_icon_t icon)
{
  xerintosh_user_item_t *_item = (xerintosh_user_item_t*)malloc(sizeof(xerintosh_user_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, user_item, _content, icon, user_icon);
  _item->init_function = _init_function;
  _item->loop_function = _loop_function;
  _item->exit_function = _exit_function;
  return (xerintosh_list_item_t*)_item;
}

xerintosh_selector_t xerintosh_selector = {};

xerintosh_selector_t *xerintosh_get_selector()
{
  return &xerintosh_selector;
}

static uint8_t find_item_index(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_target)
{
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    if (_parent->child_list_item[i] == _target)
      return i;
  }
  return 0;
}

bool xerintosh_bind_item_to_selector(xerintosh_list_item_t *_item)
{
  if (_item == NULL) return false;
  if (_item->parent == NULL) return false; // root item has no parent

  //坐标在refresh内部更新
  if (xerintosh_selector.selected_item == NULL)
  {
    xerintosh_selector.y_selector = 2 * SCREEN_HEIGHT;  //给个初始坐标做动画
    xerintosh_selector.h_selector = 160;
  }
  xerintosh_selector.selected_index = find_item_index(_item->parent, _item);
  xerintosh_selector.selected_item = _item;

  return true;
}

bool xerintosh_refresh_list_value = true;

void xerintosh_selector_go_next_item()
{
  if (xerintosh_selector.selected_item->type == slider_item && xerintosh_to_slider_item(xerintosh_selector.selected_item)->is_confirmed)
  {
    xerintosh_slider_item_t* _selected_slider_item = xerintosh_to_slider_item(xerintosh_selector.selected_item);
    *_selected_slider_item->value += _selected_slider_item->value_step;
    if (*_selected_slider_item->value >= _selected_slider_item->value_max) *_selected_slider_item->value = _selected_slider_item->value_max;
    return;
  }

  if (xerintosh_selector.selected_item->type == user_item && xerintosh_to_user_item(xerintosh_selector.selected_item)->in_user_item) return;

  xerintosh_refresh_list_value = true;

  //到达最末端
  if (xerintosh_selector.selected_index == xerintosh_selector.selected_item->parent->child_num - 1)
  {
    xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent->child_list_item[0];
    xerintosh_selector.selected_index = 0;
    return;
  }

  xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent->child_list_item[++xerintosh_selector.selected_index];
}

void xerintosh_selector_go_prev_item()
{
  if (xerintosh_selector.selected_item->type == slider_item && xerintosh_to_slider_item(xerintosh_selector.selected_item)->is_confirmed)
  {
    xerintosh_slider_item_t* _selected_slider_item = xerintosh_to_slider_item(xerintosh_selector.selected_item);
    *_selected_slider_item->value -= _selected_slider_item->value_step;
    if (*_selected_slider_item->value <= _selected_slider_item->value_min) *_selected_slider_item->value = _selected_slider_item->value_min;
    return;
  }

  if (xerintosh_selector.selected_item->type == user_item && xerintosh_to_user_item(xerintosh_selector.selected_item)->in_user_item) return;

  xerintosh_refresh_list_value = true;

  //到达最前端
  if (xerintosh_selector.selected_index == 0)
  {
    xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent->child_list_item[xerintosh_selector.selected_item->parent->child_num - 1];
    xerintosh_selector.selected_index = xerintosh_selector.selected_item->parent->child_num - 1;
    return;
  }

  xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent->child_list_item[--xerintosh_selector.selected_index];
}

bool xerintosh_exit_animation_finished = true;

static void handle_user_item_enter(xerintosh_user_item_t *_user_item)
{
  xerintosh_exit_animation_finished = false;
  _user_item->entering_user_item = true;
  _user_item->exiting_user_item = false;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

static void handle_user_item_exit(xerintosh_user_item_t *_user_item)
{
  xerintosh_exit_animation_finished = false;
  _user_item->entering_user_item = false;
  _user_item->exiting_user_item = true;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

static void handle_slider_confirm_toggle(xerintosh_slider_item_t *_slider)
{
  if (!_slider->is_confirmed)
  {
    _slider->is_confirmed = true;
    _slider->value_backup = *_slider->value;
    return;
  }
  if (_slider->exit_function)
    _slider->exit_function();
  _slider->is_confirmed = false;
}

/** @brief 确认当前选择的item
  * @note 如果选择了list 就进入选择的list
  * @note 如果选择了特殊item 就翻转/调整对应的值
  */
void xerintosh_selector_jump_to_selected_item()
{
  if (!in_xerintosh) return;

  if (xerintosh_selector.selected_item->type == user_item)
  {
    handle_user_item_enter(xerintosh_to_user_item(xerintosh_selector.selected_item));
    return;
  }

  if (xerintosh_selector.selected_item->type == switch_item)
  {
    xerintosh_switch_item_t* _selected_switch_item = xerintosh_to_switch_item(xerintosh_selector.selected_item);
    *_selected_switch_item->value = !*_selected_switch_item->value;
    if (_selected_switch_item->exit_function)
      _selected_switch_item->exit_function();
    return;
  }

  if (xerintosh_selector.selected_item->type == button_item)
  {
    xerintosh_button_item_t* _selected_button_item = xerintosh_to_button_item(xerintosh_selector.selected_item);
    if (_selected_button_item->exit_function)
      _selected_button_item->exit_function();
    return;
  }

  if (xerintosh_selector.selected_item->type == slider_item)
  {
    handle_slider_confirm_toggle(xerintosh_to_slider_item(xerintosh_selector.selected_item));
    return;
  }

  if (xerintosh_selector.selected_item->child_num == 0) return;

  xerintosh_refresh_list_value = true;

  //给选择的item的子item坐标清零 做动画
  for (uint8_t i = 0; i < xerintosh_selector.selected_item->child_num; i++)
    xerintosh_selector.selected_item->child_list_item[i]->y_list_item = 0;

  xerintosh_selector.selected_index = 0;
  xerintosh_selector.selected_item = xerintosh_selector.selected_item->child_list_item[0];
}

void xerintosh_selector_exit_current_item()
{
  if (xerintosh_selector.selected_item->type == slider_item && xerintosh_to_slider_item(xerintosh_selector.selected_item)->is_confirmed)
  {
    xerintosh_slider_item_t* _selected_slider_item = xerintosh_to_slider_item(xerintosh_selector.selected_item);
    _selected_slider_item->is_confirmed = false;
    *_selected_slider_item->value = _selected_slider_item->value_backup;
    return;
  }

  if (xerintosh_selector.selected_item->type == user_item && xerintosh_to_user_item(xerintosh_selector.selected_item)->in_user_item)
  {
    handle_user_item_exit(xerintosh_to_user_item(xerintosh_selector.selected_item));
    return;
  }

  xerintosh_refresh_list_value = true;

  if (xerintosh_selector.selected_item->parent->layer == 0 && in_xerintosh)
  {
    return;  // 主菜单没有上一级，不允许退出
  }

  //给选择的item的父item的父item的所有子item坐标清零 做动画
  for (uint8_t i = 0; i < xerintosh_selector.selected_item->parent->parent->child_num; i++)
      xerintosh_selector.selected_item->parent->parent->child_list_item[i]->y_list_item = 0;

  xerintosh_selector.selected_index = find_item_index(xerintosh_selector.selected_item->parent->parent, xerintosh_selector.selected_item->parent);
  xerintosh_selector.selected_item = xerintosh_selector.selected_item->parent;
}

bool xerintosh_push_item_to_list(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_child)
{
  if (_parent == NULL) return false;
  if (_child == NULL) return false;
  if (_parent->child_num >= MAX_LIST_CHILD_NUM) return false;
  if (_parent->layer >= MAX_LIST_LAYER) return false;

  _child->layer = _parent->layer + 1;
  _child->child_num = 0;

  xerintosh_set_font(hal_get_cn_font());
  if (_parent->child_num == 0) _child->y_list_item_trg = oled_get_str_height() + LIST_FONT_TOP_MARGIN - 1;
  else _child->y_list_item_trg = _parent->child_list_item[_parent->child_num - 1]->y_list_item_trg + LIST_ITEM_SPACING;

  if (_parent->layer == 0 && _parent->child_num == 0)
  {
    xerintosh_bind_item_to_selector(_child);  //初始化并绑定selector
    xerintosh_bind_selector_to_camera(&xerintosh_selector);  //初始化并绑定camera
  }

  _parent->child_list_item[_parent->child_num++] = _child;
  _child->parent = _parent;

  return true;
}

bool xerintosh_remove_item_from_list(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_child)
{
  if (_parent == NULL || _child == NULL) return false;

  uint8_t idx = 0;
  for (; idx < _parent->child_num; idx++)
  {
    if (_parent->child_list_item[idx] == _child)
      break;
  }
  if (idx >= _parent->child_num) return false;

  // Shift remaining children down
  for (uint8_t i = idx; i < _parent->child_num - 1; i++)
  {
    _parent->child_list_item[i] = _parent->child_list_item[i + 1];
  }
  _parent->child_num--;
  _parent->child_list_item[_parent->child_num] = NULL;

  // Recalculate y_list_item_trg for remaining children
  xerintosh_set_font(hal_get_cn_font());
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    if (i == 0)
      _parent->child_list_item[i]->y_list_item_trg = oled_get_str_height() + LIST_FONT_TOP_MARGIN - 1;
    else
      _parent->child_list_item[i]->y_list_item_trg = _parent->child_list_item[i - 1]->y_list_item_trg + LIST_ITEM_SPACING;
  }

  if (_child->content) {
    free((void*)_child->content);
    _child->content = NULL;
  }
  if (_child->user_data) {
    free(_child->user_data);
    _child->user_data = NULL;
  }
  free(_child);
  return true;
}

void xerintosh_clear_children_of_list(xerintosh_list_item_t *_parent)
{
  if (_parent == NULL) return;
  while (_parent->child_num > 0)
  {
    xerintosh_remove_item_from_list(_parent, _parent->child_list_item[_parent->child_num - 1]);
  }
}

xerintosh_camera_t xerintosh_camera = {0, 0, 0, 0}; //在refresh加上camera的坐标

void xerintosh_bind_selector_to_camera(xerintosh_selector_t *_selector)
{
  if (_selector == NULL) return;

  xerintosh_camera.selector = _selector;  //坐标在refresh内部更新
}
