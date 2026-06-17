/**
 * @file   ui_core.c
 * @brief  Xerintosh UI 核心引擎实现
 * @details 实现动画插值、主循环调度、位置刷新及 user_item 生命周期管理。
 *          所有 UI 帧的更新与渲染均由 xerintosh_ui_main_core 统筹调度。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_core.h"
#include "ui_dirty.h"
#include <stdio.h>
#include "ui_drawer.h"
#include <math.h>
#include "app/shutdown/power_key_popup.h"

/* ═══ 生命周期 ═══ */

/**
 * @brief 查询当前是否处于 user_item 内部
 * @return true  当前选中项为 user_item 且已处于运行态
 */
bool xerintosh_is_in_user_item()
{
  xerintosh_list_item_t *sel = g_xerintosh_selector.selected_item;
  if (sel == NULL) return false;

  xerintosh_user_item_t *user = xerintosh_to_user_item(sel);
  return (user != NULL && user->in_user_item);
}

/* xerintosh_mark_dirty() / xerintosh_invalidate() 实现已迁移到 ui_dirty.c */

/* ═══ 动画工具 ═══ */

/**
 * @brief  通用缓动动画函数
 * @param  _pos     当前位置指针（会被直接更新）
 * @param  _pos_trg 目标位置
 * @param  _speed   动画速度（0~99，越大越快）
 * @note   公式：current += (target - current) / (100 - speed)
 * @note   当 g_anim_enabled 为 false 时直接跳转到目标位置
 */
void xerintosh_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (!g_anim_enabled) {
      *_pos = _pos_trg;
      return;
    }
    if (_speed >= 99.0f) _speed = 99.0f;
    if (fabsf(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
    else {
      *_pos += (_pos_trg - *_pos) / (100.0f - _speed);
      xerintosh_invalidate();  /* 动画进行中，标记需要重绘 */
    }
  }
}

/* ═══ 控件位置刷新 ═══ */

/**
 * @brief 刷新信息栏位置与宽度
 */
static void xerintosh_refresh_info_bar()
{
  xerintosh_animation(&g_xerintosh_info_bar.y_info_bar, g_xerintosh_info_bar.y_info_bar_trg, ANIM_SPEED_INFO_BAR);
  xerintosh_animation(&g_xerintosh_info_bar.w_info_bar, g_xerintosh_info_bar.w_info_bar_trg, ANIM_SPEED_INFO_BAR_W);
}

/**
 * @brief 刷新弹窗位置与宽度
 */
static void xerintosh_refresh_pop_up()
{
  xerintosh_animation(&g_xerintosh_pop_up.y_pop_up, g_xerintosh_pop_up.y_pop_up_trg, ANIM_SPEED_POP_UP_Y);
  xerintosh_animation(&g_xerintosh_pop_up.w_pop_up, g_xerintosh_pop_up.w_pop_up_trg, ANIM_SPEED_POP_UP_W);
}

/**
 * @brief 刷新相机位置，确保选择器始终处于可视区域
 * @note  15 为选择器高度；向下或向上越界时自动调整相机偏移
 */
void xerintosh_refresh_camera_position()
{
  if (g_xerintosh_camera.selector == NULL) return;
  if (g_xerintosh_camera.selector->selected_item == NULL) return;

  /* 15 为选择器高度 */
  if (g_xerintosh_camera.selector->y_selector_trg + 15 + g_xerintosh_camera.y_camera_trg > SCREEN_HEIGHT)  /* 向下超出屏幕，需要向下移动 */
    g_xerintosh_camera.y_camera_trg = SCREEN_HEIGHT - g_xerintosh_camera.selector->y_selector_trg - 15;

  if (g_xerintosh_camera.selector->y_selector_trg + g_xerintosh_camera.y_camera_trg < 0)  /* 向上超出屏幕，需要向上移动 */
    g_xerintosh_camera.y_camera_trg = 0 - g_xerintosh_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  xerintosh_animation(&g_xerintosh_camera.x_camera, g_xerintosh_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  xerintosh_animation(&g_xerintosh_camera.y_camera, g_xerintosh_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}

/**
 * @brief 初始化列表动画起始位置
 * @note  将所有根节点子项的 y 坐标归零，用于入场动画
 */
static void xerintosh_init_list()
{
  /* 做动画：子项从屏幕外滑入 */
  for (uint8_t i = 0; i < xerintosh_get_root_list()->child_num; i++)
    xerintosh_get_root_list()->child_list_item[i]->y_list_item = 0;
  g_xerintosh_selector.selected_index = 0;
  g_xerintosh_selector.selected_item = xerintosh_get_root_list()->child_list_item[0];
  g_xerintosh_selector.y_selector = SCREEN_HEIGHT;
  g_xerintosh_selector.h_selector = SCREEN_HEIGHT;
}

/**
 * @brief 初始化 UI 核心状态（列表、选择器、相机绑定）
 */
void xerintosh_init_core()
{
  xerintosh_init_list();
  xerintosh_list_item_t *root = xerintosh_get_root_list();
  if (root != NULL && root->child_num > 0)
    xerintosh_bind_item_to_selector(root->child_list_item[0]);
  xerintosh_bind_selector_to_camera(&g_xerintosh_selector);
}

/**
 * @brief 刷新当前列表所有子项的目标位置（动画插值）
 */
void xerintosh_refresh_list_item_position()
{
  xerintosh_list_item_t *sel = g_xerintosh_selector.selected_item;
  if (sel == NULL || sel->parent == NULL) return;

  for (uint8_t i = 0; i < sel->parent->child_num; i++)
    xerintosh_animation(&sel->parent->child_list_item[i]->y_list_item,
                     sel->parent->child_list_item[i]->y_list_item_trg,
                     ANIM_SPEED_LIST_ITEM);
}

/**
 * @brief 刷新选择器的位置与尺寸（基于当前选中项）
 */
void xerintosh_refresh_selector_position()
{
  if (g_xerintosh_selector.selected_item == NULL) return;

  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_selector.y_selector_trg = g_xerintosh_selector.selected_item->y_list_item_trg - hal_get_font_height() + 1;
  g_xerintosh_selector.w_selector_trg = xerintosh_dispatch_measure(g_xerintosh_selector.selected_item);
  g_xerintosh_selector.h_selector_trg = hal_get_font_height() + 4;
  xerintosh_animation(&g_xerintosh_selector.y_selector, g_xerintosh_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&g_xerintosh_selector.w_selector, g_xerintosh_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&g_xerintosh_selector.h_selector, g_xerintosh_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
}

/* ═══ 主循环 ═══ */

/**
 * @brief UI 控件刷新调度（信息栏、弹窗）
 */
void xerintosh_ui_widget_core()
{
  xerintosh_refresh_info_bar();
  xerintosh_refresh_pop_up();
  xerintosh_draw_info_bar();
  xerintosh_draw_pop_up();
}

/**
 * @brief  处理 user_item 生命周期（进入→运行→退出）
 * @note   每帧调用一次，管理 init/loop/exit 回调的触发时机
 */
static void xerintosh_ui_update_lifecycle(void)
{
  xerintosh_list_item_t *item = g_xerintosh_selector.selected_item;
  if (item == NULL || item->type != user_item) return;

  xerintosh_user_item_t *user = xerintosh_to_user_item(item);

  /* 进入阶段：退场动画完成后触发 init */
  if (!user->in_user_item && user->entering_user_item
      && g_xerintosh_exit_animation_status == 1)
  {
    if (user->init_function != NULL)
      user->init_function(item->user_data);
    user->in_user_item = 1;
    user->entering_user_item = false;
    xerintosh_invalidate();
  }

  /* 运行阶段：每帧调用 loop */
  if (user->in_user_item && user->loop_function != NULL)
    user->loop_function(item->user_data);

  /* 外部 kill 请求 */
  if (g_xerintosh_exit_requested) {
    g_xerintosh_exit_requested = false;
    user->exiting_user_item = true;
    xerintosh_invalidate();
  }

  /* 退出阶段：退场动画完成后触发 exit */
  if (user->exiting_user_item && g_xerintosh_exit_animation_status == 1)
  {
    if (user->exit_function != NULL)
      user->exit_function(item->user_data);
    user->in_user_item = 0;
    user->exiting_user_item = false;
    xerintosh_invalidate();
  }

  /* 兜底：动画已完成但标志未重置时强制清理，防止退出状态残留 */
  if (user->exiting_user_item && g_xerintosh_exit_animation_finished) {
    user->in_user_item = 0;
    user->exiting_user_item = false;
    xerintosh_invalidate();
  }
}

/**
 * @brief  渲染当前帧（列表模式）或由 user_item 自行渲染
 */
static void xerintosh_ui_render_frame(void)
{
  if (xerintosh_is_in_user_item()) return; /* user_item 自行绘制 */

  xerintosh_refresh_list_item_position();
  xerintosh_refresh_selector_position();
  xerintosh_refresh_camera_position();   /* 在所有位置更新完成后计算（避免使用上一帧的 y_selector_trg） */
  xerintosh_draw_list();
}

/**
 * @brief UI 主循环核心调度
 * @note  处理 user_item 生命周期、列表刷新、退场动画等
 */
void xerintosh_ui_main_core()
{
  if (!g_in_xerintosh) return;
  if (g_xerintosh_selector.selected_item == NULL) return;

  /* 生命周期处理每帧必须运行（app loop 依赖它处理输入和退出检测） */
  xerintosh_ui_update_lifecycle();

  /* 退场动画进行中时强制重绘，确保动画不会被脏矩形跳过 */
  if (!g_xerintosh_exit_animation_finished)
    xerintosh_invalidate();

  /* 脏矩形帧跳过：列表层静态画面无需重绘 */
  if (!xerintosh_is_dirty()) return;

  /* 清除脏标志：后续动画/输入若需要重绘，会重新设置 */
  xerintosh_clear_dirty();

  xerintosh_ui_render_frame();

  /* 退场动画遮罩（双键关机模式下跳过） */
  if (!g_xerintosh_exit_animation_finished && !power_key_popup_is_dual_active())
    xerintosh_draw_exit_animation();
}
