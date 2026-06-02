/**
 * @file   ui_selector.h
 * @brief  Xerintosh UI 选择器头文件
 * @details 定义选择器结构体、导航 API 及安全移出/重建锚定等辅助函数。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_SELECTOR_H
#define UI_SELECTOR_H

#include "ui_item_core.h"
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 选择器 ═══ */

/**
 * @brief 选择器结构体（高亮框状态）
 */
typedef struct xerintosh_selector_t
{
  float y_selector, y_selector_trg, w_selector, w_selector_trg, h_selector, h_selector_trg;  /* 位置与尺寸 */
  uint8_t selected_index;        /* 当前选中索引 */
  xerintosh_list_item_t *selected_item;  /* 当前选中项指针 */
} xerintosh_selector_t;

/* 全局选择器实例由 ui_context.h 的向后兼容宏 g_xerintosh_selector 提供 */

/**
 * @brief  将指定项绑定到选择器
 * @param  _item 要绑定的列表项
 * @return true  绑定成功
 * @return false 绑定失败（参数为 NULL 或父项为 NULL）
 */
extern bool xerintosh_bind_item_to_selector(xerintosh_list_item_t *_item);

/**
 * @brief 选择器移至下一项（循环）
 * @note  若当前为 slider_item 编辑模式，则增加数值
 */
extern void xerintosh_selector_go_next_item(void);

/**
 * @brief 选择器移至上一项（循环）
 * @note  若当前为 slider_item 编辑模式，则减少数值
 */
extern void xerintosh_selector_go_prev_item(void);

/**
 * @brief 确认/进入当前选中的项
 * @note  根据项类型执行不同操作：list_item 进入子菜单、switch_item 翻转值、
 *        slider_item 切换确认态、button_item 触发回调、user_item 进入全屏 App
 */
extern void xerintosh_selector_jump_to_selected_item(void);

/**
 * @brief 返回/退出当前项
 * @note  根据项类型执行不同操作：slider_item 取消编辑、user_item 退出、
 *        list_item 返回父菜单；主菜单（layer==0）不允许退出
 */
extern void xerintosh_selector_exit_current_item(void);

/**
 * @brief  user_item 通用退出检测（供 App loop 调用）
 * @param  event_b 按钮 B 的事件
 * @return true 若已触发退出，false 若事件不匹配
 * @note   长按 B 时自动退出当前 user_item，App 无需自行实现退出逻辑
 */
extern bool ui_user_item_try_exit(hal_event_t event_b);

/**
 * @brief  安全移出选择器：若选择器位于即将被移除的子树内，将其移回父项
 * @param  subtree_root     即将被移除的子树根节点
 * @param  fallback_parent  选择器移出后的目标父项
 */
extern void ui_selector_safety_move_out(xerintosh_list_item_t *subtree_root,
                                        xerintosh_list_item_t *fallback_parent);

/**
 * @brief  rebuild 锚定：若选择器位于子树内，将其提升到子树根节点
 * @param  subtree_root 即将被重建的子树根节点
 * @param  parent       subtree_root 的父项（用于计算 selected_index）
 */
extern void ui_selector_rebuild_anchor(xerintosh_list_item_t *subtree_root,
                                       xerintosh_list_item_t *parent);

/**
 * @brief  若选择器当前在 parent 上，移至其第一个子项
 * @param  parent 父项指针
 */
extern void ui_selector_move_to_first_child(xerintosh_list_item_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* UI_SELECTOR_H */
