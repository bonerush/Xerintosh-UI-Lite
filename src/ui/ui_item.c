/**
 * @file   ui_item.c
 * @brief  Xerintosh UI 菜单项系统实现
 * @details 实现菜单项的创建、类型转换、选择器导航、相机绑定、
 *          信息栏与弹窗的管理，以及列表项的增删查改。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"

#include <stdlib.h>
#include <string.h>

#include "ui_core.h"

/* ═══ 字体 ═══ */

/**
 * @brief 设置当前绘图字体（仅当字体变化时才更新 HAL）
 * @param _font 字体指针
 */
void xerintosh_set_font(const void *_font)
{
  if (_font != g_xerintosh_font) oled_set_font(_font);
}

/* ═══ 信息栏 ═══ */

xerintosh_info_bar_t g_xerintosh_info_bar = {0, 1, 0 - 2 * INFO_BAR_HEIGHT, 0 - 2 * INFO_BAR_HEIGHT, 80, 80, false, 0, 1};

/**
 * @brief 推送顶部信息栏
 * @param _content 显示文本
 * @param _span    显示持续时间（毫秒）
 * @note   如果信息栏正在显示相同内容，则重置计时器；否则重新展开
 */
void xerintosh_push_info_bar(const char *_content, const uint16_t _span)
{
  /* 设定显示时间的概念：超过了显示时间，就将 y_trg 设为初始位置；
     如果在显示时间之内有新的消息涌入，则 y 和 y_trg 都不变，继续显示，且显示时间清零。
     只有显示时间到了的时候，才会复位。 */

  g_xerintosh_info_bar.time = get_ticks();
  g_xerintosh_info_bar.content = _content;
  g_xerintosh_info_bar.span = _span;
  g_xerintosh_info_bar.is_running = false; /* 每次进入该函数都代表有新的消息涌入，所以需要重置 is_running */

  /* 展开弹窗；收回弹窗和同步时间戳需要在循环中进行，所以移到了 drawer 中 */
  if (!g_xerintosh_info_bar.is_running)
  {
    g_xerintosh_info_bar.time_start = get_ticks();
    g_xerintosh_info_bar.y_info_bar_trg = 0;
    g_xerintosh_info_bar.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_info_bar.w_info_bar_trg = oled_get_UTF8_width(g_xerintosh_info_bar.content) + INFO_BAR_OFFSET;
}

/* ═══ 弹窗 ═══ */

xerintosh_pop_up_t g_xerintosh_pop_up = {0, 1, 0 - 2 * POP_UP_HEIGHT, 0 - 2 * POP_UP_HEIGHT, 80, 80, false, 0, 1};

/**
 * @brief 推送中部弹窗
 * @param _content 显示文本
 * @param _span    显示持续时间（毫秒）
 * @note   如果弹窗正在显示相同内容，则重置计时器并提升位置
 */
void xerintosh_push_pop_up(const char *_content, const uint16_t _span)
{
  if (g_xerintosh_pop_up.is_running && g_xerintosh_pop_up.content != NULL
      && strcmp(g_xerintosh_pop_up.content, _content) == 0) {
    g_xerintosh_pop_up.time_start = get_ticks();
    g_xerintosh_pop_up.span = _span;
    g_xerintosh_pop_up.y_pop_up_trg = 20;
    return;
  }

  g_xerintosh_pop_up.time = get_ticks();
  g_xerintosh_pop_up.content = _content;
  g_xerintosh_pop_up.span = _span;
  g_xerintosh_pop_up.is_running = false;

  /* 弹出 */
  if (!g_xerintosh_pop_up.is_running)
  {
    g_xerintosh_pop_up.time_start = get_ticks();
    g_xerintosh_pop_up.y_pop_up_trg = 20;
    g_xerintosh_pop_up.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_pop_up.w_pop_up_trg = oled_get_UTF8_width(g_xerintosh_pop_up.content) + POP_UP_OFFSET;
}

/**
 * @brief 立即隐藏弹窗（将位置重置到屏幕外）
 */
void xerintosh_hide_pop_up(void)
{
  g_xerintosh_pop_up.is_running = false;
  g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
  g_xerintosh_pop_up.y_pop_up = 0 - 2 * POP_UP_HEIGHT;
}

/* ═══ 类型转换 ═══ */

/**
 * @brief  安全类型转换辅助函数
 * @param  _item          列表项指针
 * @param  _expected_type 期望的类型
 * @return 原指针（类型匹配时）或根节点指针（类型不匹配时）
 * @note   类型不匹配时返回根节点，防止空指针解引用
 */
static xerintosh_list_item_t *xerintosh_safe_cast(xerintosh_list_item_t *_item,
                                          xerintosh_list_item_type_t _expected_type)
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

/* ═══ 列表项创建 ═══ */

/**
 * @brief 初始化列表项基类字段
 * @param _item         要初始化的项指针
 * @param _type         项类型
 * @param _content      显示文本
 * @param _icon         用户指定的图标
 * @param _default_icon 默认图标（当用户指定 default_icon 时使用）
 * @note  content 会通过 strdup 复制，需要确保后续释放
 */
static void xerintosh_init_base_item(xerintosh_list_item_t *_item,
                                  xerintosh_list_item_type_t _type,
                                  const char *_content,
                                  xerintosh_list_item_icon_t _icon,
                                  xerintosh_list_item_icon_t _default_icon)
{
  memset(_item, 0, sizeof(xerintosh_list_item_t));
  _item->type = _type;
  _item->content = _content ? strdup(_content) : NULL;
  _item->icon = (_icon == default_icon) ? _default_icon : _icon;
}

/* tips: 不会重复创建 root 节点 */
/**
 * @brief  获取根节点（单例）
 * @return 根节点指针；内存分配失败时返回 NULL
 */
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

/**
 * @brief  创建普通列表项
 * @param  _content 显示文本
 * @param  icon     图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
xerintosh_list_item_t *xerintosh_new_list_item(const char *_content, xerintosh_list_item_icon_t icon)
{
  xerintosh_list_item_t *_item = (xerintosh_list_item_t*)malloc(sizeof(xerintosh_list_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(_item, list_item, _content, icon, list_icon);
  return _item;
}

/**
 * @brief  创建开关项
 * @param  _content       显示文本
 * @param  _value         绑定的布尔值指针
 * @param  _init_function 进入该项时调用的初始化函数（可为 NULL）
 * @param  _exit_function 值改变后调用的退出函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
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

/**
 * @brief  创建按钮项
 * @param  _content       显示文本
 * @param  _exit_function 按下时触发的回调函数
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
xerintosh_list_item_t *xerintosh_new_button_item(const char *_content, void (*_exit_function)(),
                                          xerintosh_list_item_icon_t icon)
{
  xerintosh_button_item_t *_item = (xerintosh_button_item_t*)malloc(sizeof(xerintosh_button_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, button_item, _content, icon, plus_icon);
  _item->exit_function = _exit_function;
  return (xerintosh_list_item_t*)_item;
}

/**
 * @brief  创建滑块项
 * @param  _content       显示文本
 * @param  _value         绑定的数值指针
 * @param  _step          步进值
 * @param  _min           最小值
 * @param  _max           最大值
 * @param  _init_function 进入该项时调用的初始化函数（可为 NULL）
 * @param  _exit_function 值改变后调用的退出函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
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

/**
 * @brief  创建用户自定义项（全屏 App 入口）
 * @param  _content       显示文本
 * @param  _init_function 进入时调用一次的初始化函数（可为 NULL）
 * @param  _loop_function 每帧调用的循环函数（可为 NULL）
 * @param  _exit_function 退出时调用一次的清理函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
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

/* ═══ 选择器 ═══ */

xerintosh_selector_t g_xerintosh_selector = {};

/**
 * @brief  获取选择器指针
 * @return 选择器指针
 */
xerintosh_selector_t *xerintosh_get_selector()
{
  return &g_xerintosh_selector;
}

/**
 * @brief  在父项的子项列表中查找目标项的索引
 * @param  _parent 父项指针
 * @param  _target 目标子项指针
 * @return 索引值；未找到时返回 0
 */
static uint8_t find_item_index(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_target)
{
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    if (_parent->child_list_item[i] == _target)
      return i;
  }
  return 0;
}

/**
 * @brief  将指定项绑定到选择器
 * @param  _item 要绑定的列表项
 * @return true  绑定成功
 * @return false 绑定失败（参数为 NULL 或父项为 NULL）
 * @note   首次绑定时会给选择器一个屏幕外的初始坐标，以便播放滑入动画
 */
bool xerintosh_bind_item_to_selector(xerintosh_list_item_t *_item)
{
  if (_item == NULL) return false;
  if (_item->parent == NULL) return false; /* root item has no parent */

  /* 坐标在 refresh 内部更新 */
  if (g_xerintosh_selector.selected_item == NULL)
  {
    g_xerintosh_selector.y_selector = 2 * SCREEN_HEIGHT;  /* 给个初始坐标做动画 */
    g_xerintosh_selector.h_selector = 160;
  }
  g_xerintosh_selector.selected_index = find_item_index(_item->parent, _item);
  g_xerintosh_selector.selected_item = _item;

  return true;
}

/* ═══ 全局标志定义 ═══ */

bool g_xerintosh_refresh_list_value = true;

/* ═══ 选择器导航 ═══ */

/**
 * @brief 选择器移至下一项（循环）
 * @note  若当前为 slider_item 编辑模式，则增加数值；
 *        若已处于 user_item 内部，则忽略
 */
void xerintosh_selector_go_next_item()
{
  if (g_xerintosh_selector.selected_item->type == slider_item
      && xerintosh_to_slider_item(g_xerintosh_selector.selected_item)->is_confirmed)
  {
    xerintosh_slider_item_t* _selected_slider_item = xerintosh_to_slider_item(g_xerintosh_selector.selected_item);
    *_selected_slider_item->value += _selected_slider_item->value_step;
    if (*_selected_slider_item->value >= _selected_slider_item->value_max)
      *_selected_slider_item->value = _selected_slider_item->value_max;
    return;
  }

  if (g_xerintosh_selector.selected_item->type == user_item
      && xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item) return;

  g_xerintosh_refresh_list_value = true;

  /* 到达最末端 */
  if (g_xerintosh_selector.selected_index == g_xerintosh_selector.selected_item->parent->child_num - 1)
  {
    g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->parent->child_list_item[0];
    g_xerintosh_selector.selected_index = 0;
    return;
  }

  g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->parent->child_list_item[++g_xerintosh_selector.selected_index];
}

/**
 * @brief 选择器移至上一项（循环）
 * @note  若当前为 slider_item 编辑模式，则减少数值；
 *        若已处于 user_item 内部，则忽略
 */
void xerintosh_selector_go_prev_item()
{
  if (g_xerintosh_selector.selected_item->type == slider_item
      && xerintosh_to_slider_item(g_xerintosh_selector.selected_item)->is_confirmed)
  {
    xerintosh_slider_item_t* _selected_slider_item = xerintosh_to_slider_item(g_xerintosh_selector.selected_item);
    *_selected_slider_item->value -= _selected_slider_item->value_step;
    if (*_selected_slider_item->value <= _selected_slider_item->value_min)
      *_selected_slider_item->value = _selected_slider_item->value_min;
    return;
  }

  if (g_xerintosh_selector.selected_item->type == user_item
      && xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item) return;

  g_xerintosh_refresh_list_value = true;

  /* 到达最前端 */
  if (g_xerintosh_selector.selected_index == 0)
  {
    g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->parent->child_list_item[
      g_xerintosh_selector.selected_item->parent->child_num - 1];
    g_xerintosh_selector.selected_index = g_xerintosh_selector.selected_item->parent->child_num - 1;
    return;
  }

  g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->parent->child_list_item[--g_xerintosh_selector.selected_index];
}

/* ═══ 退场动画标志 ═══ */

bool g_xerintosh_exit_animation_finished = true;

/* ═══ user_item / slider 辅助函数 ═══ */

/**
 * @brief 处理 user_item 进入状态重置
 * @param _user_item 目标 user_item
 */
static void handle_user_item_enter(xerintosh_user_item_t *_user_item)
{
  g_xerintosh_exit_animation_finished = false;
  _user_item->entering_user_item = true;
  _user_item->exiting_user_item = false;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

/**
 * @brief 处理 user_item 退出状态重置
 * @param _user_item 目标 user_item
 */
static void handle_user_item_exit(xerintosh_user_item_t *_user_item)
{
  g_xerintosh_exit_animation_finished = false;
  _user_item->entering_user_item = false;
  _user_item->exiting_user_item = true;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

/**
 * @brief 处理滑块项确认态切换
 * @param _slider 目标 slider_item
 * @note  首次确认时备份原值；再次确认时触发 exit_function
 */
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

/**
 * @brief 确认/进入当前选中的项
 * @note  根据项类型执行不同操作：
 *        - user_item：进入全屏 App
 *        - switch_item：翻转布尔值
 *        - button_item：触发回调
 *        - slider_item：切换确认态
 *        - list_item：进入子菜单
 */
void xerintosh_selector_jump_to_selected_item()
{
  if (!g_in_xerintosh) return;

  if (g_xerintosh_selector.selected_item->type == user_item)
  {
    handle_user_item_enter(xerintosh_to_user_item(g_xerintosh_selector.selected_item));
    return;
  }

  if (g_xerintosh_selector.selected_item->type == switch_item)
  {
    xerintosh_switch_item_t* _selected_switch_item = xerintosh_to_switch_item(g_xerintosh_selector.selected_item);
    *_selected_switch_item->value = !*_selected_switch_item->value;
    if (_selected_switch_item->exit_function)
      _selected_switch_item->exit_function();
    return;
  }

  if (g_xerintosh_selector.selected_item->type == button_item)
  {
    xerintosh_button_item_t* _selected_button_item = xerintosh_to_button_item(g_xerintosh_selector.selected_item);
    if (_selected_button_item->exit_function)
      _selected_button_item->exit_function();
    return;
  }

  if (g_xerintosh_selector.selected_item->type == slider_item)
  {
    handle_slider_confirm_toggle(xerintosh_to_slider_item(g_xerintosh_selector.selected_item));
    return;
  }

  if (g_xerintosh_selector.selected_item->child_num == 0) return;

  g_xerintosh_refresh_list_value = true;

  /* 给选择的 item 的子 item 坐标清零，做动画 */
  for (uint8_t i = 0; i < g_xerintosh_selector.selected_item->child_num; i++)
    g_xerintosh_selector.selected_item->child_list_item[i]->y_list_item = 0;

  g_xerintosh_selector.selected_index = 0;
  g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->child_list_item[0];
}

/**
 * @brief 返回/退出当前项
 * @note  根据项类型执行不同操作：
 *        - slider_item 编辑模式：取消修改并恢复备份值
 *        - user_item 运行态：触发退出流程
 *        - 主菜单（layer==0）：不允许退出
 *        - 其他：返回父菜单
 */
void xerintosh_selector_exit_current_item()
{
  if (g_xerintosh_selector.selected_item->type == slider_item
      && xerintosh_to_slider_item(g_xerintosh_selector.selected_item)->is_confirmed)
  {
    xerintosh_slider_item_t* _selected_slider_item = xerintosh_to_slider_item(g_xerintosh_selector.selected_item);
    _selected_slider_item->is_confirmed = false;
    *_selected_slider_item->value = _selected_slider_item->value_backup;
    return;
  }

  if (g_xerintosh_selector.selected_item->type == user_item
      && xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item)
  {
    handle_user_item_exit(xerintosh_to_user_item(g_xerintosh_selector.selected_item));
    return;
  }

  g_xerintosh_refresh_list_value = true;

  if (g_xerintosh_selector.selected_item->parent->layer == 0 && g_in_xerintosh)
  {
    return;  /* 主菜单没有上一级，不允许退出 */
  }

  /* 给选择的 item 的父 item 的父 item 的所有子 item 坐标清零，做动画 */
  for (uint8_t i = 0; i < g_xerintosh_selector.selected_item->parent->parent->child_num; i++)
      g_xerintosh_selector.selected_item->parent->parent->child_list_item[i]->y_list_item = 0;

  g_xerintosh_selector.selected_index = find_item_index(
    g_xerintosh_selector.selected_item->parent->parent, g_xerintosh_selector.selected_item->parent);
  g_xerintosh_selector.selected_item = g_xerintosh_selector.selected_item->parent;
}

/* ═══ 列表项挂载与移除 ═══ */

/**
 * @brief  将子项挂载到父项下
 * @param  _parent 父项指针
 * @param  _child  子项指针
 * @return true   挂载成功
 * @return false  挂载失败（子项已满、层级超限、参数为 NULL）
 * @note   会自动设置子项层级、计算纵向目标坐标；
 *         首次挂载到根节点时会自动绑定选择器和相机
 */
bool xerintosh_push_item_to_list(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_child)
{
  if (_parent == NULL) return false;
  if (_child == NULL) return false;
  if (_parent->child_num >= MAX_LIST_CHILD_NUM) return false;
  if (_parent->layer >= MAX_LIST_LAYER) return false;

  _child->layer = _parent->layer + 1;

  xerintosh_set_font(hal_get_cn_font());
  if (_parent->child_num == 0)
    _child->y_list_item_trg = oled_get_str_height() + LIST_FONT_TOP_MARGIN - 1;
  else
    _child->y_list_item_trg = _parent->child_list_item[_parent->child_num - 1]->y_list_item_trg + LIST_ITEM_SPACING;

  if (_parent->layer == 0 && _parent->child_num == 0)
  {
    xerintosh_bind_item_to_selector(_child);  /* 初始化并绑定 selector */
    xerintosh_bind_selector_to_camera(&g_xerintosh_selector);  /* 初始化并绑定 camera */
  }

  _parent->child_list_item[_parent->child_num++] = _child;
  _child->parent = _parent;

  return true;
}

/**
 * @brief  从父项中移除指定子项
 * @param  _parent 父项指针
 * @param  _child  要移除的子项指针
 * @return true   移除成功
 * @return false  移除失败（未找到、参数为 NULL）
 * @note   会自动释放子项内存及其 content、user_data
 */
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

  /* 将后续子项前移填补空缺 */
  for (uint8_t i = idx; i < _parent->child_num - 1; i++)
  {
    _parent->child_list_item[i] = _parent->child_list_item[i + 1];
  }
  _parent->child_num--;
  _parent->child_list_item[_parent->child_num] = NULL;

  /* 重新计算剩余子项的目标坐标 */
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

/**
 * @brief  清空父项下的所有子项
 * @param  _parent 父项指针
 * @note   循环调用 xerintosh_remove_item_from_list 直到子项数为 0
 */
void xerintosh_clear_children_of_list(xerintosh_list_item_t *_parent)
{
  if (_parent == NULL) return;
  while (_parent->child_num > 0)
  {
    xerintosh_remove_item_from_list(_parent, _parent->child_list_item[_parent->child_num - 1]);
  }
}

/* ═══ 相机 ═══ */

xerintosh_camera_t g_xerintosh_camera = {0, 0, 0, 0}; /* 在 refresh 中加上 camera 的坐标 */

/**
 * @brief  将选择器绑定到相机
 * @param  _selector 选择器指针
 * @note   绑定后相机会跟随选择器移动，确保其始终处于可视区域
 */
void xerintosh_bind_selector_to_camera(xerintosh_selector_t *_selector)
{
  if (_selector == NULL) return;

  g_xerintosh_camera.selector = _selector;  /* 坐标在 refresh 内部更新 */
}
