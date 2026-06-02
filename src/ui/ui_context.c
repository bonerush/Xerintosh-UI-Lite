/**
 * @file   ui_context.c
 * @brief  Xerintosh UI 全局上下文实现
 * @details 实现全局上下文单例，内部存储所有 UI 状态。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_context.h"
#include "ui_item.h"   /* for xerintosh_selector_t, xerintosh_camera_t 等完整类型 */

/* ═══ 内部存储（不再暴露为全局符号）═══ */

static xerintosh_selector_t s_selector = {};
static xerintosh_camera_t s_camera = {0, 0, 0, 0, NULL};
static xerintosh_info_bar_t s_info_bar = {0, 1, 0 - 2 * 15, 0 - 2 * 15, 80, 80, false, 0, 1};
static xerintosh_pop_up_t s_pop_up = {0, 1, 0 - 2 * 48, 0 - 2 * 48, 80, 80, false, 0, 1, {NULL, NULL, NULL}, 0};

/* ═══ 全局上下文单例 ═══ */

static xerintosh_context_t g_ui_ctx = {
  .in_xerintosh = false,
  .anim_enabled = true,
  .exit_requested = false,
  .exit_animation_status = 0,
  .exit_animation_finished = true,
  .refresh_list_value = true,
  .draw_color = 0xFFFF,
  .anim_speed = 92,
  .selector = &s_selector,
  .camera = &s_camera,
  .info_bar = &s_info_bar,
  .pop_up = &s_pop_up,
};

xerintosh_context_t *xerintosh_get_context(void)
{
  return &g_ui_ctx;
}

void xerintosh_context_init(void)
{
  /* 重置标量状态 */
  g_ui_ctx.in_xerintosh = false;
  g_ui_ctx.anim_enabled = true;
  g_ui_ctx.exit_requested = false;
  g_ui_ctx.exit_animation_status = 0;
  g_ui_ctx.exit_animation_finished = true;
  g_ui_ctx.refresh_list_value = true;
  g_ui_ctx.draw_color = 0xFFFF;
  g_ui_ctx.anim_speed = 92;

  /* 重置子系统状态 */
  s_selector = (xerintosh_selector_t){};
  s_camera = (xerintosh_camera_t){0, 0, 0, 0, &s_selector};
  s_info_bar = (xerintosh_info_bar_t){0, 1, 0 - 2 * 15, 0 - 2 * 15, 80, 80, false, 0, 1};
  s_pop_up = (xerintosh_pop_up_t){0, 1, 0 - 2 * 48, 0 - 2 * 48, 80, 80, false, 0, 1, {NULL, NULL, NULL}, 0};

  /* 重新连接指针 */
  g_ui_ctx.selector = &s_selector;
  g_ui_ctx.camera = &s_camera;
  g_ui_ctx.info_bar = &s_info_bar;
  g_ui_ctx.pop_up = &s_pop_up;
}
