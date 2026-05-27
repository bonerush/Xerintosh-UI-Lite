/**
 * @file   ui_item_selector.c
 * @brief  选择器导航与绑定
 * @details 实现选择器状态管理、上下导航、确认/进入、退出/返回等操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"
#include "ui_core.h"
#include "ui_drawer.h"
#include "kernel/kern_task.h"
#include <stddef.h>

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
  g_xerintosh_exit_animation_status = 0;  /* 重置动画状态机 */
  _user_item->entering_user_item = true;
  _user_item->exiting_user_item = false;

  /* 注册虚任务，使 App 对内核可见（/proc/tasks 可见、kill 可终止） */
  if (_user_item->kernel_pid == KERN_PID_INVALID) {
    _user_item->kernel_pid = kern_task_register_virtual(_user_item->base_item.content);
  }
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

  /* 注销虚任务，从内核任务链表移除 */
  if (_user_item->kernel_pid != KERN_PID_INVALID) {
    kern_task_unregister_virtual(_user_item->kernel_pid);
    _user_item->kernel_pid = KERN_PID_INVALID;
  }
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
