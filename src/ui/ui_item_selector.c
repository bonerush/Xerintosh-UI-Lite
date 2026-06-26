/**
 * @file   ui_item_selector.c
 * @brief  选择器导航与绑定
 * @details 实现选择器状态管理、上下导航、确认/进入、退出/返回等操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"
#include "ui_core.h"
#include "ui_dirty.h"
#include "ui_drawer.h"
#include "hal/hal_input.h"
#include <stddef.h>

/* ═══ 选择器 ═══ */

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
    g_xerintosh_selector.y_selector = 2 * HAL_SCREEN_HEIGHT;  /* 给个初始坐标做动画 */
    g_xerintosh_selector.h_selector = HAL_SCREEN_HEIGHT;
  }
  g_xerintosh_selector.selected_index = find_item_index(_item->parent, _item);
  g_xerintosh_selector.selected_item = _item;

  /* 弹簧动画：切换目标时清零速度，防止旧速度影响新目标 */
  g_xerintosh_selector.v_y_selector = 0.0f;
  g_xerintosh_selector.v_w_selector = 0.0f;
  g_xerintosh_selector.v_h_selector = 0.0f;

  xerintosh_invalidate();

  return true;
}

/* ═══ 选择器导航 ═══ */

/**
 * @brief 选择器移至下一项（循环）
 * @note  优先通过派发表处理类型特定输入（如 slider 编辑模式）；
 *        若未被消费，则执行默认循环导航。
 */
void xerintosh_selector_go_next_item()
{
  if (g_xerintosh_selector.selected_item == NULL) return;
  if (xerintosh_dispatch_input_next(g_xerintosh_selector.selected_item)) return;

  g_xerintosh_refresh_list_value = true;
  xerintosh_invalidate();

  /* 局部变量缓存解引用，减少重复指针链访问 */
  xerintosh_list_item_t *parent = g_xerintosh_selector.selected_item->parent;
  if (parent == NULL) return;  /* 根节点无父项，防御性返回 */
  xerintosh_list_item_t **children = parent->child_list_item;
  int16_t count = (int16_t)parent->child_num;
  if (count == 0) return;  /* 子项被清空后不能导航 */

  /* 到达最末端 */
  if (g_xerintosh_selector.selected_index == count - 1)
  {
    g_xerintosh_selector.selected_item = children[0];
    g_xerintosh_selector.selected_index = 0;
    /* 弹簧动画：边界跳转时清零速度 */
    g_xerintosh_selector.v_y_selector = 0.0f;
    g_xerintosh_selector.v_w_selector = 0.0f;
    g_xerintosh_selector.v_h_selector = 0.0f;
    return;
  }

  g_xerintosh_selector.selected_item = children[++g_xerintosh_selector.selected_index];
}

/**
 * @brief 选择器移至上一项（循环）
 * @note  优先通过派发表处理类型特定输入（如 slider 编辑模式）；
 *        若未被消费，则执行默认循环导航。
 */
void xerintosh_selector_go_prev_item()
{
  if (g_xerintosh_selector.selected_item == NULL) return;
  if (xerintosh_dispatch_input_prev(g_xerintosh_selector.selected_item)) return;

  g_xerintosh_refresh_list_value = true;
  xerintosh_invalidate();

  /* 局部变量缓存解引用，减少重复指针链访问 */
  xerintosh_list_item_t *parent = g_xerintosh_selector.selected_item->parent;
  if (parent == NULL) return;  /* 根节点无父项，防御性返回 */
  xerintosh_list_item_t **children = parent->child_list_item;
  int16_t count = (int16_t)parent->child_num;
  if (count == 0) return;  /* 子项被清空后不能导航 */

  /* 到达最前端 */
  if (g_xerintosh_selector.selected_index == 0)
  {
    g_xerintosh_selector.selected_item = children[count - 1];
    g_xerintosh_selector.selected_index = count - 1;
    /* 弹簧动画：边界跳转时清零速度 */
    g_xerintosh_selector.v_y_selector = 0.0f;
    g_xerintosh_selector.v_w_selector = 0.0f;
    g_xerintosh_selector.v_h_selector = 0.0f;
    return;
  }

  g_xerintosh_selector.selected_item = children[--g_xerintosh_selector.selected_index];
}

/**
 * @brief 确认/进入当前选中的项
 * @note  通过 xerintosh_dispatch_enter 派发表路由到具体类型处理器
 */
void xerintosh_selector_jump_to_selected_item()
{
  if (!g_in_xerintosh) return;
  if (g_xerintosh_selector.selected_item == NULL) return;
  xerintosh_dispatch_enter(g_xerintosh_selector.selected_item);
  xerintosh_invalidate();
}

/**
 * @brief 返回/退出当前项
 * @note  优先通过派发表处理类型特定输入（如 slider 取消编辑、user_item 触发退出）；
 *        若未被消费，则执行默认返回导航。
 */
void xerintosh_selector_exit_current_item()
{
  if (g_xerintosh_selector.selected_item == NULL) return;
  if (xerintosh_dispatch_input_exit(g_xerintosh_selector.selected_item)) return;

  g_xerintosh_refresh_list_value = true;
  xerintosh_invalidate();

  xerintosh_list_item_t *parent = g_xerintosh_selector.selected_item->parent;
  if (parent == NULL) return;  /* 根节点无法退出 */
  if (parent->layer == 0 && g_in_xerintosh)
  {
    return;  /* 主菜单没有上一级，不允许退出 */
  }

  xerintosh_list_item_t *grandparent = parent->parent;
  if (grandparent == NULL) return;  /* 防御性：无祖父节点时无法回退 */

  /* 给选择的 item 的父 item 的父 item 的所有子 item 坐标清零，做动画 */
  for (uint8_t i = 0; i < grandparent->child_num; i++)
      grandparent->child_list_item[i]->y_list_item = 0;

  g_xerintosh_selector.selected_index = find_item_index(grandparent, parent);
  g_xerintosh_selector.selected_item = parent;

  /* 重置选择器到 y=0，与列表项同步从顶部滑入，避免子菜单残留位置导致的动画突变 */
  g_xerintosh_selector.y_selector = 0.0f;

  /* 弹簧动画：切换父菜单时清零速度，保证回退动画从头弹起 */
  g_xerintosh_selector.v_y_selector = 0.0f;
  g_xerintosh_selector.v_w_selector = 0.0f;
  g_xerintosh_selector.v_h_selector = 0.0f;
}

/* ═══ 公共 Helper ═══ */

bool ui_user_item_try_exit(hal_event_t event_b)
{
    if (event_b != HAL_EVENT_LONG_PRESS) return false;

    xerintosh_user_item_t *current =
        xerintosh_to_user_item(g_xerintosh_selector.selected_item);
    if (current != NULL && !current->exiting_user_item) {
        xerintosh_selector_exit_current_item();
    }
    return true;
}

void ui_selector_rebuild_anchor(xerintosh_list_item_t *subtree_root,
                                xerintosh_list_item_t *parent)
{
    if (subtree_root == NULL) return;

    /* 若选择器位于即将被重建的子树内，将其提升到子树根节点 */
    xerintosh_list_item_t *check = g_xerintosh_selector.selected_item;
    while (check && check != subtree_root) {
        check = check->parent;
    }
    if (check == subtree_root) {
        uint8_t idx = 0;
        if (parent != NULL) {
            for (uint8_t i = 0; i < parent->child_num; i++) {
                if (parent->child_list_item[i] == subtree_root) {
                    idx = i;
                    break;
                }
            }
        }
        g_xerintosh_selector.selected_item  = subtree_root;
        g_xerintosh_selector.selected_index = idx;
        g_xerintosh_selector.v_y_selector = 0.0f;  /* 弹簧动画：锚点重建时清零速度 */
        g_xerintosh_selector.v_w_selector = 0.0f;
        g_xerintosh_selector.v_h_selector = 0.0f;
        xerintosh_invalidate();
    }
}

void ui_selector_safety_move_out(xerintosh_list_item_t *subtree_root,
                                 xerintosh_list_item_t *fallback_parent)
{
    if (subtree_root == NULL) return;

    /* 检查选择器是否位于即将被移除的子树内（包括等于子树根节点本身） */
    xerintosh_list_item_t *check = g_xerintosh_selector.selected_item;
    while (check && check != subtree_root) {
        check = check->parent;
    }
    if (check != subtree_root) return;

    if (fallback_parent == NULL) {
        g_xerintosh_selector.selected_item = NULL;
        g_xerintosh_selector.selected_index = 0;
        xerintosh_invalidate();
        return;
    }

    /* 优先移回 fallback_parent 中不在待移除子树内的子项 */
    xerintosh_list_item_t *safe_item = NULL;
    uint8_t safe_index = 0;
    for (uint8_t i = 0; i < fallback_parent->child_num; i++) {
        xerintosh_list_item_t *cand = fallback_parent->child_list_item[i];
        if (cand == subtree_root) continue;
        safe_item = cand;
        safe_index = i;
        break;
    }

    if (safe_item != NULL) {
        g_xerintosh_selector.selected_item  = safe_item;
        g_xerintosh_selector.selected_index = safe_index;
    } else {
        /* 所有子项都在待移除子树内（例如移除的是唯一子项），则回退到父项 */
        g_xerintosh_selector.selected_item = fallback_parent;
        if (fallback_parent->parent != NULL) {
            g_xerintosh_selector.selected_index = find_item_index(
                fallback_parent->parent, fallback_parent);
        } else {
            g_xerintosh_selector.selected_index = 0;
        }
    }

    g_xerintosh_selector.v_y_selector = 0.0f;  /* 弹簧动画：安全移出时清零速度 */
    g_xerintosh_selector.v_w_selector = 0.0f;
    g_xerintosh_selector.v_h_selector = 0.0f;
    xerintosh_invalidate();
}
