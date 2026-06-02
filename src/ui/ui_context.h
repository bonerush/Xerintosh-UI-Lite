/**
 * @file   ui_context.h
 * @brief  Xerintosh UI 全局上下文
 * @details 将所有分散的 UI 全局状态收拢到单一结构体中，
 *          提供单例获取接口和向后兼容的标量宏。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_CONTEXT_H
#define UI_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明（避免与 ui_item.h 循环包含） */
struct xerintosh_selector_t;
struct xerintosh_camera_t;
struct xerintosh_info_bar_t;
struct xerintosh_pop_up_t;

/* ═══ 全局上下文结构体 ═══ */

typedef struct xerintosh_context_t
{
  /* 核心状态 */
  bool in_xerintosh;                  /* UI 是否处于激活状态 */
  bool anim_enabled;                  /* 动画是否启用 */
  bool exit_requested;                /* 外部请求退出当前 user_item */
  uint8_t exit_animation_status;      /* 退场动画阶段状态机 */
  bool exit_animation_finished;       /* 退场动画是否已完成 */
  bool refresh_list_value;            /* 是否需要刷新列表项显示值 */

  /* 绘制状态 */
  uint16_t draw_color;                /* 当前前景色 */
  int16_t anim_speed;                 /* 全局动画速度基准值 */

  /* 子系统状态（指针，指向 ui_context.c 内部存储） */
  struct xerintosh_selector_t *selector;
  struct xerintosh_camera_t *camera;
  struct xerintosh_info_bar_t *info_bar;
  struct xerintosh_pop_up_t *pop_up;
} xerintosh_context_t;

/* ═══ 单例接口 ═══ */

/**
 * @brief 获取 UI 全局上下文单例
 * @return 全局上下文指针（永不返回 NULL）
 */
xerintosh_context_t *xerintosh_get_context(void);

/**
 * @brief 初始化 UI 全局上下文（在 xerintosh_init_core 之前调用）
 * @note  用于测试隔离或在重新初始化 UI 前重置状态
 */
void xerintosh_context_init(void);

/* ═══ 向后兼容宏：标量类型 ═══ */
/* 这些宏可以在不知道完整结构体类型的情况下使用 */

#define g_in_xerintosh                       (xerintosh_get_context()->in_xerintosh)
#define g_anim_enabled                       (xerintosh_get_context()->anim_enabled)
#define g_xerintosh_exit_requested           (xerintosh_get_context()->exit_requested)
#define g_xerintosh_exit_animation_status    (xerintosh_get_context()->exit_animation_status)
#define g_xerintosh_exit_animation_finished  (xerintosh_get_context()->exit_animation_finished)
#define g_xerintosh_refresh_list_value       (xerintosh_get_context()->refresh_list_value)
#define g_xerintosh_draw_color               (xerintosh_get_context()->draw_color)
#define g_anim_speed                         (xerintosh_get_context()->anim_speed)

/* ═══ 向后兼容宏：结构体实例（需完整类型定义，在 ui_item.h 末尾提供）═══ */
/* #define g_xerintosh_selector  (*(xerintosh_get_context()->selector)) */
/* #define g_xerintosh_camera    (*(xerintosh_get_context()->camera))   */
/* #define g_xerintosh_info_bar  (*(xerintosh_get_context()->info_bar))  */
/* #define g_xerintosh_pop_up    (*(xerintosh_get_context()->pop_up))    */

#ifdef __cplusplus
}
#endif

#endif /* UI_CONTEXT_H */
