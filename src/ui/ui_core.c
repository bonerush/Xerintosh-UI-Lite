/**
 * @file   ui_core.c
 * @brief  Xerintosh UI 核心引擎实现
 * @details 实现动画插值、主循环调度、位置刷新及 user_item 生命周期管理。
 *          所有 UI 帧的更新与渲染均由 xerintosh_ui_main_core 统筹调度。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_core.h"
#include <stdio.h>
#include "ui_drawer.h"
#include <math.h>

/* ═══ 全局状态定义 ═══ */

bool g_in_xerintosh = false;                 /* UI 是否处于激活状态 */
uint16_t g_xerintosh_draw_color = 0xFFFF;    /* 当前前景色（默认白色） */
bool g_anim_enabled = true;                  /* 动画是否启用 */

/* ═══ 生命周期 ═══ */

/**
 * @brief 查询当前是否处于 user_item 内部
 * @return true  当前选中项为 user_item 且已处于运行态
 */
bool xerintosh_is_in_user_item()
{
  return (g_xerintosh_selector.selected_item->type == user_item
          && xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item)
         ? true : false;
}

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
    if (fabs(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
    else *_pos += (_pos_trg - *_pos) / (100.0f - _speed) / 1.0f;
  }
}

/* ═══ 控件位置刷新 ═══ */

/**
 * @brief 刷新信息栏位置与宽度
 */
void xerintosh_refresh_info_bar()
{
  xerintosh_animation(&g_xerintosh_info_bar.y_info_bar, g_xerintosh_info_bar.y_info_bar_trg, ANIM_SPEED_INFO_BAR);
  xerintosh_animation(&g_xerintosh_info_bar.w_info_bar, g_xerintosh_info_bar.w_info_bar_trg, ANIM_SPEED_INFO_BAR_W);
}

/**
 * @brief 刷新弹窗位置与宽度
 */
void xerintosh_refresh_pop_up()
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
  /* 15 为选择器高度 */
  if (g_xerintosh_camera.selector->y_selector_trg + 15 + g_xerintosh_camera.y_camera_trg > SCREEN_HEIGHT)  /* 向下超出屏幕，需要向下移动 */
    g_xerintosh_camera.y_camera_trg = SCREEN_HEIGHT - g_xerintosh_camera.selector->y_selector_trg - 15;

  if (g_xerintosh_camera.selector->y_selector_trg + g_xerintosh_camera.y_camera_trg < 0)  /* 向上超出屏幕，需要向上移动 */
    g_xerintosh_camera.y_camera_trg = 0 - g_xerintosh_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  xerintosh_animation(&g_xerintosh_camera.x_camera, g_xerintosh_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  xerintosh_animation(&g_xerintosh_camera.y_camera, g_xerintosh_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}

/**
 * @brief 刷新控件核心位置（信息栏 + 弹窗）
 */
void xerintosh_refresh_widget_core_position()
{
  /* 需要调用所有的 widget refresh 函数 */
  xerintosh_refresh_info_bar();
  xerintosh_refresh_pop_up();
}

/**
 * @brief 初始化列表动画起始位置
 * @note  将所有根节点子项的 y 坐标归零，用于入场动画
 */
void xerintosh_init_list()
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
  if (root->child_num > 0)
    xerintosh_bind_item_to_selector(root->child_list_item[0]);
  else
    xerintosh_bind_item_to_selector(root);
  xerintosh_bind_selector_to_camera(xerintosh_get_selector());
}

/**
 * @brief 刷新当前列表所有子项的目标位置（动画插值）
 */
void xerintosh_refresh_list_item_position()
{
  for (uint8_t i = 0; i < g_xerintosh_selector.selected_item->parent->child_num; i++)
    xerintosh_animation(&g_xerintosh_selector.selected_item->parent->child_list_item[i]->y_list_item,
                     g_xerintosh_selector.selected_item->parent->child_list_item[i]->y_list_item_trg,
                     ANIM_SPEED_LIST_ITEM);
}

/**
 * @brief 刷新选择器的位置与尺寸（基于当前选中项）
 */
void xerintosh_refresh_selector_position()
{
  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_selector.y_selector_trg = g_xerintosh_selector.selected_item->y_list_item_trg - hal_get_font_height() + 1;
  if (g_xerintosh_selector.selected_item->type == switch_item || g_xerintosh_selector.selected_item->type == slider_item)
    g_xerintosh_selector.w_selector_trg = SCREEN_WIDTH - 18;
  else g_xerintosh_selector.w_selector_trg = hal_get_utf8_width(g_xerintosh_selector.selected_item->content) + 12;
  g_xerintosh_selector.h_selector_trg = hal_get_font_height() + 4;
  xerintosh_animation(&g_xerintosh_selector.y_selector, g_xerintosh_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&g_xerintosh_selector.w_selector, g_xerintosh_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
  xerintosh_animation(&g_xerintosh_selector.h_selector, g_xerintosh_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
}

/**
 * @brief 刷新主核心位置（调用列表刷新）
 */
void xerintosh_refresh_main_core_position()
{
  xerintosh_refresh_list_item_position();
}

/* ═══ 主循环 ═══ */

/**
 * @brief UI 控件刷新调度（信息栏、弹窗）
 */
void xerintosh_ui_widget_core()
{
  xerintosh_refresh_widget_core_position();
  xerintosh_draw_widget();
}

/**
 * @brief UI 主循环核心调度
 * @note  处理 user_item 生命周期、列表刷新、退场动画等
 */
void xerintosh_ui_main_core()
{
  if (!g_in_xerintosh) return;

  /* 切换 in_user_item 的逻辑 */
  if (g_xerintosh_selector.selected_item->type == user_item
      && !xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item)
  {
    xerintosh_user_item_t *_selected_user_item = xerintosh_to_user_item(g_xerintosh_selector.selected_item);

    if (_selected_user_item->entering_user_item && g_xerintosh_exit_animation_status == 1)
    {
      if (_selected_user_item->init_function != NULL)
        _selected_user_item->init_function();
      _selected_user_item->in_user_item = 1;
    }
  }

  /* 渲染逻辑：user_item 内部由 App 自行绘制；列表模式由框架绘制 */
  if (g_xerintosh_selector.selected_item->type == user_item
      && xerintosh_to_user_item(g_xerintosh_selector.selected_item)->in_user_item)
  {
    xerintosh_user_item_t* _selected_user_item = xerintosh_to_user_item(g_xerintosh_selector.selected_item);

    if (_selected_user_item->loop_function != NULL)
    {
      _selected_user_item->loop_function();
    }

    if (_selected_user_item->exiting_user_item && g_xerintosh_exit_animation_status == 1)
    {
        if (_selected_user_item->exit_function != NULL)
            _selected_user_item->exit_function();
        _selected_user_item->in_user_item = 0;
        _selected_user_item->exiting_user_item = false;
    }
  } else
  {
    xerintosh_refresh_camera_position();
    xerintosh_refresh_main_core_position();
    xerintosh_refresh_selector_position();
    xerintosh_draw_list();
  }

  /* 退场动画 */
  /* 上面都是正常应当绘制的内容；退场动画需要绘制时，只需在上面的基础上绘制遮罩即可 */
  if (!g_xerintosh_exit_animation_finished)
    xerintosh_draw_exit_animation();
}
