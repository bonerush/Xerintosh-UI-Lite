/**
 * @file   ui_item_list.c
 * @brief  列表项挂载与移除
 * @details 实现菜单树中子项的挂载、移除及清空操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"

#include <stdlib.h>
#include <string.h>

#include "ui_core.h"

/**
 * @brief  重新计算父项所有子项的目标 Y 坐标
 * @param  _parent 父项指针
 */
static void recalc_child_y_positions(xerintosh_list_item_t *_parent)
{
  if (_parent == NULL) return;

  xerintosh_set_font(hal_get_cn_font());
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    if (i == 0)
      _parent->child_list_item[i]->y_list_item_trg = hal_get_font_height() + LIST_FONT_TOP_MARGIN - 1;
    else
      _parent->child_list_item[i]->y_list_item_trg = _parent->child_list_item[i - 1]->y_list_item_trg + LIST_ITEM_SPACING;
  }
}

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
    _child->y_list_item_trg = hal_get_font_height() + LIST_FONT_TOP_MARGIN - 1;
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
  recalc_child_y_positions(_parent);

  /* 若选择器位于即将移除的子树内，先安全移出，避免悬垂指针 */
  ui_selector_safety_move_out(_child, _parent);

  /* 递归释放子节点及其后代 */
  xerintosh_destroy_item_tree(_child);
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

  /* 递归释放所有子节点 */
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    xerintosh_destroy_item_tree(_parent->child_list_item[i]);
    _parent->child_list_item[i] = NULL;
  }
  _parent->child_num = 0;
}

/**
 * @brief  递归释放菜单项及其所有子节点
 * @param  _item 要释放的菜单项指针
 * @note   会调用 item 的 destroy_callback（如果存在）
 * @note   此函数不负责从父项的 child_list_item 数组中移除指针
 */
void xerintosh_destroy_item_tree(xerintosh_list_item_t *_item)
{
  if (_item == NULL) return;

  /* 递归释放所有子节点 */
  for (uint8_t i = 0; i < _item->child_num; i++)
  {
    xerintosh_destroy_item_tree(_item->child_list_item[i]);
    _item->child_list_item[i] = NULL;
  }
  _item->child_num = 0;

  /* 通过派发表执行类型特定的销毁清理（如 user_item 的 destroy_callback） */
  xerintosh_dispatch_destroy(_item);

  /* 释放 content（由 strdup 分配） */
  if (_item->content != NULL)
  {
    free((void *)_item->content);
    _item->content = NULL;
  }

  /* 释放 item 本身 */
  free(_item);
}
