/**
 * @file   ui_item_base.c
 * @brief  列表项基类与创建
 * @details 实现根列表单例、五种菜单项的创建及安全类型转换。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"

#include <stdlib.h>
#include <string.h>

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
  return NULL;
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
                                          xerintosh_cb_t _init_function, xerintosh_cb_t _exit_function,
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
xerintosh_list_item_t *xerintosh_new_button_item(const char *_content, xerintosh_cb_t _exit_function,
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
                                          xerintosh_cb_t _init_function, xerintosh_cb_t _exit_function,
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
xerintosh_list_item_t *xerintosh_new_user_item(const char *_content, xerintosh_cb_t _init_function,
                                        xerintosh_cb_t _loop_function, xerintosh_cb_t _exit_function,
                                        xerintosh_list_item_icon_t icon)
{
  xerintosh_user_item_t *_item = (xerintosh_user_item_t*)malloc(sizeof(xerintosh_user_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, user_item, _content, icon, user_icon);
  _item->in_user_item = false;
  _item->entering_user_item = false;
  _item->exiting_user_item = false;
  _item->init_function = _init_function;
  _item->loop_function = _loop_function;
  _item->exit_function = _exit_function;
  _item->destroy_callback = NULL;
  _item->kernel_pid    = NULL;
  return (xerintosh_list_item_t*)_item;
}
