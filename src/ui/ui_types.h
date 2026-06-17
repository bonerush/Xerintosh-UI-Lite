/**
 * @file   ui_types.h
 * @brief  Xerintosh UI 基础类型与常量定义
 * @details 统一存放枚举、回调类型、动画速度常量、列表项布局常量
 *          及全局标志变量的声明，供各子模块共享。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_TYPES_H
#define UI_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 动画速度常量 ═══ */
/* g_anim_speed 由 ui_context.h 的向后兼容宏提供 */

#define ANIM_SPEED_LIST_ITEM    (g_anim_speed - 8) /* 列表项动画速度，适当慢于选择器 */
#define ANIM_SPEED_SELECTOR     (g_anim_speed)     /* 选择器动画速度 */
#define ANIM_SPEED_SELECTOR_H   (g_anim_speed + 1) /* 选择器高度动画稍快 */
#define ANIM_SPEED_INFO_BAR     (g_anim_speed + 2) /* 信息栏动画速度 */
#define ANIM_SPEED_INFO_BAR_W   (g_anim_speed + 3) /* 信息栏宽度动画稍快 */
#define ANIM_SPEED_POP_UP_W     (g_anim_speed + 4) /* 弹窗宽度动画稍快 */
#define ANIM_SPEED_POP_UP_Y     (g_anim_speed + 2) /* 弹窗 y 轴动画 */
#define ANIM_SPEED_CAMERA       (g_anim_speed + 4) /* 相机动画速度 */
#define ANIM_SPEED_EXIT         (g_anim_speed + 2) /* 退出动画速度 */

/* ═══ 弹簧动画参数（运行时可调） ═══ */
extern float g_spring_stiffness_selector;  /* 弹簧刚度，默认 0.20f（设置>弹簧硬度） */
extern float g_spring_damping_selector;    /* 弹簧阻尼，默认 0.35f（设置>反弹力度） */
extern bool  g_spring_anim_mode;           /* true=动弹弹力, false=普通一阶 */

#define SPRING_STIFFNESS_SELECTOR_DEFAULT  0.20f
#define SPRING_DAMPING_SELECTOR_DEFAULT    0.35f

/* ═══ 字体 ═══ */

/**
 * @brief 设置当前绘图字体
 * @param _font 字体指针
 */
extern void xerintosh_set_font(const void* _font);

/* ═══ 全局标志 ═══ */
/* g_xerintosh_exit_animation_finished 和 g_xerintosh_refresh_list_value
   由 ui_context.h 的向后兼容宏提供 */

/* ═══ 回调类型 ═══ */

/**
 * @brief 统一回调函数类型
 * @param user_data 用户上下文指针（来自 xerintosh_list_item_t.user_data）
 */
typedef void (*xerintosh_cb_t)(void *user_data);

/* ═══ 列表项类型枚举 ═══ */

/**
 * @brief 菜单项类型枚举
 */
typedef enum
{
  list_item,
  switch_item,
  slider_item,
  user_item,
  button_item,
} xerintosh_list_item_type_t;

/**
 * @brief 列表项图标类型枚举
 */
typedef enum {
    default_icon,
    list_icon,
    switch_icon,
    plus_icon,
    user_icon,
    slider_icon,
    flag_icon,
    power_icon,
    custom_icon,    /* 自定义位图图标，需配合 bitmap_data 使用 */
} xerintosh_list_item_icon_t;

/* ═══ 列表项布局常量 ═══ */

#define MAX_LIST_CHILD_NUM 10   /* 每个父节点最多子项数 */
#define MAX_LIST_LAYER 10       /* 菜单树最大深度 */
#define LIST_ITEM_SPACING 18    /* 列表项纵向间距 */
#define LIST_ITEM_LEFT_MARGIN 4 /* 列表项左边距 */
#define LIST_ITEM_RIGHT_MARGIN 20  /* 列表项右边距（为右侧控件预留） */
#define LIST_INFO_BAR_HEIGHT 3  /* 信息栏高度补偿 */
#define LIST_FONT_TOP_MARGIN 6  /* 字体顶部边距 */

#ifdef __cplusplus
}
#endif

/**
 * @brief  判断列表项 Y 坐标是否在屏幕可视区域内
 * @param  _y_item 项的 y 坐标（屏幕坐标）
 * @return true  可见
 * @return false 不可见（超出上下边界 + 2px 容差）
 */
extern bool xerintosh_is_item_visible(int16_t _y_item);

#endif /* UI_TYPES_H */
