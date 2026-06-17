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
#include "ui_types.h"
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

/* ═══ 动画工具 ═══ */

/**
 * @brief  通用缓动动画函数
 * @param  _pos     当前位置指针（会被直接更新）
 * @param  _pos_trg 目标位置
 * @param  _speed   动画速度（自动裁剪到 ANIM_SPEED_MIN..ANIM_SPEED_MAX）
 * @return true  位置已稳定在目标
 * @return false 动画进行中（位置已更新）
 * @note   公式：current += (target - current) / (100 - speed)
 * @note   速度越大动画越快。speed=0 最慢，speed=99 最快。
 * @note   当 g_anim_enabled 为 false 时直接跳转到目标位置。
 * @note   diff <= ANIM_SNAP_THRESHOLD 时直接吸附到目标。
 */
bool xerintosh_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (!g_anim_enabled) {
      *_pos = _pos_trg;
      return true;
    }
    /* 速度边界裁剪 */
    if (_speed > ANIM_SPEED_MAX) _speed = ANIM_SPEED_MAX;
    if (_speed < ANIM_SPEED_MIN) _speed = ANIM_SPEED_MIN;
    if (fabsf(*_pos - _pos_trg) <= ANIM_SNAP_THRESHOLD) {
      *_pos = _pos_trg;
      return true;
    }
    *_pos += (_pos_trg - *_pos) / (100.0f - _speed);
    xerintosh_invalidate();  /* 动画进行中，标记需要重绘 */
    return false;
  }
  return true;
}

/* ═══ 弹簧动画（二阶欠阻尼系统） ═══ */

/**
 * @brief  弹簧动画函数
 * @details 基于自动控制理论二阶弹簧-阻尼器离散模型（additive damping）：
 *          force = stiffness * (target - pos) - damping * velocity
 *          velocity += force
 *          position += velocity
 *
 *          当 stiffness=0.10, damping=0.30 时（ζ≈0.47），产生约18%超调
 *          和1-2次可见弹跳后稳定，形成"QQ弹弹"的视觉效果。
 *
 *          稳定性分析（Euler 积分离散化）：
 *          状态转移矩阵特征值 |λ| = sqrt(1 - damping) < 1（∀ damping > 0）
 *          系统绝对收敛，永不发散。当前参数范围 stiffness∈[0.04,0.40]、
 *          damping∈[0.04,0.40] 均满足该条件。吸附阈值 0.5px 保证
 *          最终精确到位。
 *
 * @param  _pos      当前位置指针（会被直接更新）
 * @param  _vel      当前速度指针（会被直接更新）
 * @param  _pos_trg  目标位置
 * @param  _stiffness 刚度系数（越大响应越快，典型 0.05-0.20）
 * @param  _damping  粘性阻尼系数（越大衰减越快，典型 0.20-0.50）
 */
void xerintosh_spring_animation(float *_pos, float *_vel, float _pos_trg,
                                 float _stiffness, float _damping)
{
  if (!g_anim_enabled) {
    *_pos = _pos_trg;
    *_vel = 0.0f;
    return;
  }

  /* F = k*(target - x) - c*v */
  float force = _stiffness * (_pos_trg - *_pos) - _damping * (*_vel);
  *_vel += force;
  *_pos += *_vel;

  /* 靠近目标且速度足够小时吸附并清零速度 */
  if (fabsf(*_pos - _pos_trg) < 0.5f && fabsf(*_vel) < 0.5f) {
    *_pos = _pos_trg;
    *_vel = 0.0f;
    return;
  }

  xerintosh_invalidate();  /* 动画进行中，标记需要重绘 */
}

/**
 * @brief  统一动画调度：根据全局设置自动选择弹簧或缓动动画
 * @param  _pos     当前位置指针（会被直接更新）
 * @param  _vel     当前速度指针（弹簧模式使用，可为 NULL 强制缓动）
 * @param  _target  目标位置
 * @param  _speed   动画速度（0~99，仅缓动模式使用）
 * @return true  已稳定在目标
 * @return false 动画进行中
 * @note   当 g_anim_enabled == false 时直接跳转到目标。
 * @note   当 g_spring_anim_mode == true 且 _vel != NULL 时使用弹簧动画，
 *         刚度/阻尼参数使用 g_spring_stiffness_selector / g_spring_damping_selector。
 * @note   弹簧模式下返回值恒为 false（调用方需自行判断 settled）。
 */
bool xerintosh_animate_unified(float *_pos, float *_vel, float _target, float _speed)
{
    if (!g_anim_enabled) {
        *_pos = _target;
        if (_vel) *_vel = 0.0f;
        return true;
    }
    if (g_spring_anim_mode && _vel) {
        xerintosh_spring_animation(_pos, _vel, _target,
                                    g_spring_stiffness_selector,
                                    g_spring_damping_selector);
        return false; /* 弹簧不返回 settled，调用方自行判断 */
    }
    return xerintosh_animation(_pos, _target, _speed);
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
  if (g_spring_anim_mode) {
    xerintosh_spring_animation(&g_xerintosh_selector.y_selector, &g_xerintosh_selector.v_y_selector,
                                g_xerintosh_selector.y_selector_trg,
                                g_spring_stiffness_selector, g_spring_damping_selector);
    xerintosh_spring_animation(&g_xerintosh_selector.w_selector, &g_xerintosh_selector.v_w_selector,
                                g_xerintosh_selector.w_selector_trg,
                                g_spring_stiffness_selector, g_spring_damping_selector);
    xerintosh_spring_animation(&g_xerintosh_selector.h_selector, &g_xerintosh_selector.v_h_selector,
                                g_xerintosh_selector.h_selector_trg,
                                g_spring_stiffness_selector, g_spring_damping_selector);
  } else {
    xerintosh_animation(&g_xerintosh_selector.y_selector, g_xerintosh_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
    xerintosh_animation(&g_xerintosh_selector.w_selector, g_xerintosh_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
    xerintosh_animation(&g_xerintosh_selector.h_selector, g_xerintosh_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
  }
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
