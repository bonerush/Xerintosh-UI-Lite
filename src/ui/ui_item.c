#include "ui_item.h"

#include <stdlib.h>
#include <string.h>

#include "ui_core.h"

void astra_set_font(void *_font)
{
  if (_font != astra_font) oled_set_font(_font);
}

astra_info_bar_t astra_info_bar = {0, 1, 0 - 2 * INFO_BAR_HEIGHT, 0 - 2 * INFO_BAR_HEIGHT, 80, 80, false, 0, 1};

void astra_push_info_bar(const char *_content, const uint16_t _span)
{
  //设定显示时间的概念，超过了显示时间，就将ytrg设为初始位置，如果在显示时间之内，有新的消息涌入，则y和ytrg都不变，继续显示，且显示时间清零
  //只有显示时间到了的时候，才会复位

  astra_info_bar.time = get_ticks();
  astra_info_bar.content = _content;
  astra_info_bar.span = _span;
  astra_info_bar.is_running = false; //每次进入该函数都代表有新的消息涌入，所以需要重置is_running

  //展开弹窗 收回弹窗和同步时间戳需要在循环中进行 所以移到了drawer中
  if (!astra_info_bar.is_running)
  {
    astra_info_bar.time_start = get_ticks();
    astra_info_bar.y_info_bar_trg = 0;
    astra_info_bar.is_running = true;
  }

  astra_set_font(NULL);
  astra_info_bar.w_info_bar_trg = oled_get_UTF8_width(astra_info_bar.content) + INFO_BAR_OFFSET;
}

astra_pop_up_t astra_pop_up = {0, 1, 0 - 2 * POP_UP_HEIGHT, 0 - 2 * POP_UP_HEIGHT, 80, 80, false, 0, 1};

void astra_push_pop_up(const char *_content, const uint16_t _span)
{
  astra_pop_up.time = get_ticks();
  astra_pop_up.content = _content;
  astra_pop_up.span = _span;
  astra_pop_up.is_running = false;

  //弹出
  if (!astra_pop_up.is_running)
  {
    astra_pop_up.time_start = get_ticks();
    astra_pop_up.y_pop_up_trg = 20;
    astra_pop_up.is_running = true;
  }

  astra_set_font(NULL);
  astra_pop_up.w_pop_up_trg = oled_get_UTF8_width(astra_pop_up.content) + POP_UP_OFFSET;
}

// astra_list_item_t astra_list_item_root = {};

static astra_list_item_t *astra_safe_cast(astra_list_item_t *_item, astra_list_item_type_t _expected_type)
{
  if (_item != NULL && _item->type == _expected_type)
    return _item;
  return astra_get_root_list();
}

astra_switch_item_t *astra_to_switch_item(astra_list_item_t *_astra_list_item)
{
  return (astra_switch_item_t*)astra_safe_cast(_astra_list_item, switch_item);
}

astra_button_item_t *astra_to_button_item(astra_list_item_t *_astra_list_item)
{
  return (astra_button_item_t*)astra_safe_cast(_astra_list_item, button_item);
}

astra_slider_item_t *astra_to_slider_item(astra_list_item_t *_astra_list_item)
{
  return (astra_slider_item_t*)astra_safe_cast(_astra_list_item, slider_item);
}

astra_user_item_t *astra_to_user_item(astra_list_item_t *_astra_list_item)
{
  return (astra_user_item_t*)astra_safe_cast(_astra_list_item, user_item);
}

static void astra_init_base_item(astra_list_item_t *_item, astra_list_item_type_t _type,
                                  const char *_content, astra_list_item_icon_t _icon,
                                  astra_list_item_icon_t _default_icon)
{
  memset(_item, 0, sizeof(astra_list_item_t));
  _item->type = _type;
  _item->content = _content;
  _item->icon = (_icon == default_icon) ? _default_icon : _icon;
}

//tips: 不会重复创建root节点
astra_list_item_t *astra_get_root_list()
{
  static astra_list_item_t *_astra_list_root_item = NULL;
  if (_astra_list_root_item == NULL)
  {
    _astra_list_root_item = (astra_list_item_t*)malloc(sizeof(astra_list_item_t));
    if (_astra_list_root_item == NULL) return NULL;
    astra_init_base_item(_astra_list_root_item, list_item, "root", default_icon, list_icon);
  }
  return _astra_list_root_item;
}

astra_list_item_t *astra_new_list_item(const char *_content, astra_list_item_icon_t icon)
{
  astra_list_item_t *_item = (astra_list_item_t*)malloc(sizeof(astra_list_item_t));
  if (_item == NULL) return NULL;
  astra_init_base_item(_item, list_item, _content, icon, list_icon);
  return _item;
}

astra_list_item_t *astra_new_switch_item(const char *_content, bool *_value,
                                          void (*_init_function)(), void (*_exit_function)(),
                                          astra_list_item_icon_t icon)
{
  astra_switch_item_t *_item = (astra_switch_item_t*)malloc(sizeof(astra_switch_item_t));
  if (_item == NULL) return NULL;
  astra_init_base_item(&_item->base_item, switch_item, _content, icon, switch_icon);
  _item->value = _value;
  _item->init_function = _init_function;
  _item->exit_function = _exit_function;
  return (astra_list_item_t*)_item;
}

astra_list_item_t *astra_new_button_item(const char *_content, void (*_exit_function)(),
                                          astra_list_item_icon_t icon)
{
  astra_button_item_t *_item = (astra_button_item_t*)malloc(sizeof(astra_button_item_t));
  if (_item == NULL) return NULL;
  astra_init_base_item(&_item->base_item, button_item, _content, icon, plus_icon);
  _item->exit_function = _exit_function;
  return (astra_list_item_t*)_item;
}

astra_list_item_t *astra_new_slider_item(const char *_content, int16_t *_value, uint8_t _step,
                                          int16_t _min, int16_t _max,
                                          void (*_init_function)(), void (*_exit_function)(),
                                          astra_list_item_icon_t icon)
{
  astra_slider_item_t *_item = (astra_slider_item_t*)malloc(sizeof(astra_slider_item_t));
  if (_item == NULL) return NULL;
  astra_init_base_item(&_item->base_item, slider_item, _content, icon, slider_icon);
  _item->value = _value;
  _item->value_step = _step;
  _item->value_min = _min;
  _item->value_max = _max;
  _item->init_function = _init_function;
  _item->exit_function = _exit_function;
  return (astra_list_item_t*)_item;
}

astra_list_item_t *astra_new_user_item(const char *_content, void (*_init_function)(),
                                        void (*_loop_function)(), void (*_exit_function)(),
                                        astra_list_item_icon_t icon)
{
  astra_user_item_t *_item = (astra_user_item_t*)malloc(sizeof(astra_user_item_t));
  if (_item == NULL) return NULL;
  astra_init_base_item(&_item->base_item, user_item, _content, icon, user_icon);
  _item->init_function = _init_function;
  _item->loop_function = _loop_function;
  _item->exit_function = _exit_function;
  return (astra_list_item_t*)_item;
}

astra_selector_t astra_selector = {};

astra_selector_t *astra_get_selector()
{
  return &astra_selector;
}

static uint8_t find_item_index(astra_list_item_t *_parent, astra_list_item_t *_target)
{
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    if (_parent->child_list_item[i] == _target)
      return i;
  }
  return 0;
}

bool astra_bind_item_to_selector(astra_list_item_t *_item)
{
  if (_item == NULL) return false;
  if (_item->parent == NULL) return false; // root item has no parent

  //坐标在refresh内部更新
  if (astra_selector.selected_item == NULL)
  {
    astra_selector.y_selector = 2 * SCREEN_HEIGHT;  //给个初始坐标做动画
    astra_selector.h_selector = 160;
  }
  astra_selector.selected_index = find_item_index(_item->parent, _item);
  astra_selector.selected_item = _item;

  return true;
}

bool astra_refresh_list_value = true;

void astra_selector_go_next_item()
{
  if (astra_selector.selected_item->type == slider_item && astra_to_slider_item(astra_selector.selected_item)->is_confirmed)
  {
    astra_slider_item_t* _selected_slider_item = astra_to_slider_item(astra_selector.selected_item);
    *_selected_slider_item->value += _selected_slider_item->value_step;
    if (*_selected_slider_item->value >= _selected_slider_item->value_max) *_selected_slider_item->value = _selected_slider_item->value_max;
    return;
  }

  if (astra_selector.selected_item->type == user_item && astra_to_user_item(astra_selector.selected_item)->in_user_item) return;

  astra_refresh_list_value = true;

  //到达最末端
  if (astra_selector.selected_index == astra_selector.selected_item->parent->child_num - 1)
  {
    astra_selector.selected_item = astra_selector.selected_item->parent->child_list_item[0];
    astra_selector.selected_index = 0;
    return;
  }

  astra_selector.selected_item = astra_selector.selected_item->parent->child_list_item[++astra_selector.selected_index];
}

void astra_selector_go_prev_item()
{
  if (astra_selector.selected_item->type == slider_item && astra_to_slider_item(astra_selector.selected_item)->is_confirmed)
  {
    astra_slider_item_t* _selected_slider_item = astra_to_slider_item(astra_selector.selected_item);
    *_selected_slider_item->value -= _selected_slider_item->value_step;
    if (*_selected_slider_item->value <= _selected_slider_item->value_min) *_selected_slider_item->value = _selected_slider_item->value_min;
    return;
  }

  if (astra_selector.selected_item->type == user_item && astra_to_user_item(astra_selector.selected_item)->in_user_item) return;

  astra_refresh_list_value = true;

  //到达最前端
  if (astra_selector.selected_index == 0)
  {
    astra_selector.selected_item = astra_selector.selected_item->parent->child_list_item[astra_selector.selected_item->parent->child_num - 1];
    astra_selector.selected_index = astra_selector.selected_item->parent->child_num - 1;
    return;
  }

  astra_selector.selected_item = astra_selector.selected_item->parent->child_list_item[--astra_selector.selected_index];
}

bool astra_exit_animation_finished = true;

static void handle_user_item_enter(astra_user_item_t *_user_item)
{
  astra_exit_animation_finished = false;
  _user_item->entering_user_item = true;
  _user_item->exiting_user_item = false;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

static void handle_user_item_exit(astra_user_item_t *_user_item)
{
  astra_exit_animation_finished = false;
  _user_item->entering_user_item = false;
  _user_item->exiting_user_item = true;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

static void handle_slider_confirm_toggle(astra_slider_item_t *_slider)
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
void astra_selector_jump_to_selected_item()
{
  if (!in_astra) return;

  if (astra_selector.selected_item->type == user_item)
  {
    handle_user_item_enter(astra_to_user_item(astra_selector.selected_item));
    return;
  }

  if (astra_selector.selected_item->type == switch_item)
  {
    astra_switch_item_t* _selected_switch_item = astra_to_switch_item(astra_selector.selected_item);
    *_selected_switch_item->value = !*_selected_switch_item->value;
    if (_selected_switch_item->exit_function)
      _selected_switch_item->exit_function();
    return;
  }

  if (astra_selector.selected_item->type == button_item)
  {
    astra_button_item_t* _selected_button_item = astra_to_button_item(astra_selector.selected_item);
    if (_selected_button_item->exit_function)
      _selected_button_item->exit_function();
    return;
  }

  if (astra_selector.selected_item->type == slider_item)
  {
    handle_slider_confirm_toggle(astra_to_slider_item(astra_selector.selected_item));
    return;
  }

  if (astra_selector.selected_item->child_num == 0) return;

  astra_refresh_list_value = true;

  //给选择的item的子item坐标清零 做动画
  for (uint8_t i = 0; i < astra_selector.selected_item->child_num; i++)
    astra_selector.selected_item->child_list_item[i]->y_list_item = 0;

  astra_selector.selected_index = 0;
  astra_selector.selected_item = astra_selector.selected_item->child_list_item[0];
}

void astra_selector_exit_current_item()
{
  if (astra_selector.selected_item->type == slider_item && astra_to_slider_item(astra_selector.selected_item)->is_confirmed)
  {
    astra_slider_item_t* _selected_slider_item = astra_to_slider_item(astra_selector.selected_item);
    _selected_slider_item->is_confirmed = false;
    *_selected_slider_item->value = _selected_slider_item->value_backup;
    return;
  }

  if (astra_selector.selected_item->type == user_item && astra_to_user_item(astra_selector.selected_item)->in_user_item)
  {
    handle_user_item_exit(astra_to_user_item(astra_selector.selected_item));
    return;
  }

  astra_refresh_list_value = true;

  if (astra_selector.selected_item->parent->layer == 0 && in_astra)
  {
    if (ALLOW_EXIT_ASTRA_UI_BY_USER) in_astra = false;
    return;
  }

  //给选择的item的父item的父item的所有子item坐标清零 做动画
  for (uint8_t i = 0; i < astra_selector.selected_item->parent->parent->child_num; i++)
      astra_selector.selected_item->parent->parent->child_list_item[i]->y_list_item = 0;

  astra_selector.selected_index = find_item_index(astra_selector.selected_item->parent->parent, astra_selector.selected_item->parent);
  astra_selector.selected_item = astra_selector.selected_item->parent;
}

bool astra_push_item_to_list(astra_list_item_t *_parent, astra_list_item_t *_child)
{
  if (_parent == NULL) return false;
  if (_child == NULL) return false;
  if (_parent->child_num >= MAX_LIST_CHILD_NUM) return false;
  if (_parent->layer >= MAX_LIST_LAYER) return false;

  _child->layer = _parent->layer + 1;
  _child->child_num = 0;

  astra_set_font(NULL);
  if (_parent->child_num == 0) _child->y_list_item_trg = oled_get_str_height() + LIST_FONT_TOP_MARGIN - 1;
  else _child->y_list_item_trg = _parent->child_list_item[_parent->child_num - 1]->y_list_item_trg + LIST_ITEM_SPACING;

  if (_parent->layer == 0 && _parent->child_num == 0)
  {
    astra_bind_item_to_selector(_child);  //初始化并绑定selector
    astra_bind_selector_to_camera(&astra_selector);  //初始化并绑定camera
  }

  _parent->child_list_item[_parent->child_num++] = _child;
  _child->parent = _parent;

  return true;
}

astra_camera_t astra_camera = {0, 0, 0, 0}; //在refresh加上camera的坐标

void astra_bind_selector_to_camera(astra_selector_t *_selector)
{
  if (_selector == NULL) return;

  astra_camera.selector = _selector;  //坐标在refresh内部更新
}
