/**
 * @file   ui_core.h
 * @brief  Xerintosh UI 核心引擎头文件
 * @details 定义动画系统、主循环调度、位置刷新及全局状态标志。
 *          所有 UI 帧的更新与渲染均由本模块统筹。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 全局状态 ═══ */

extern bool g_in_xerintosh;                  /* UI 是否处于激活状态 */
extern bool g_anim_enabled;                  /* 动画是否启用 */
extern bool g_xerintosh_exit_requested;      /* 外部请求退出当前 user_item */
extern uint8_t g_xerintosh_exit_animation_status;  /* 退场动画阶段状态机 */

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 UI 核心状态（列表、选择器、相机绑定）
 */
extern void xerintosh_init_core(void);

/**
 * @brief 初始化列表动画起始位置
 * @note  将所有根节点子项的 y 坐标归零，用于入场动画
 */
extern void xerintosh_init_list(void);

/* ═══ 主循环 ═══ */

/**
 * @brief UI 主循环核心调度
 * @note  处理 user_item 生命周期、列表刷新、退场动画等
 */
extern void xerintosh_ui_main_core(void);

/**
 * @brief UI 控件刷新调度（信息栏、弹窗）
 */
extern void xerintosh_ui_widget_core(void);

/* ═══ 刷新位置 ═══ */

/**
 * @brief 刷新当前列表所有子项的目标位置（动画插值）
 */
extern void xerintosh_refresh_list_item_position(void);

/**
 * @brief 刷新选择器的位置与尺寸（基于当前选中项）
 */
extern void xerintosh_refresh_selector_position(void);

/**
 * @brief 刷新相机位置，确保选择器始终处于可视区域
 */
extern void xerintosh_refresh_camera_position(void);

/**
 * @brief 刷新信息栏位置与宽度
 */
extern void xerintosh_refresh_info_bar(void);

/**
 * @brief 刷新弹窗位置与宽度
 */
extern void xerintosh_refresh_pop_up(void);

/* ═══ 动画工具 ═══ */

/**
 * @brief  通用缓动动画函数
 * @param  _pos    当前位置指针（会被直接更新）
 * @param  _pos_trg 目标位置
 * @param  _speed   动画速度（0~99，越大越快）
 * @note   公式：current += (target - current) / (100 - speed)
 * @note   当 g_anim_enabled 为 false 时直接跳转到目标位置
 */
extern void xerintosh_animation(float *_pos, float _pos_trg, float _speed);

/* ═══ 状态查询 ═══ */

/**
 * @brief  查询当前是否处于 user_item 内部
 * @return true  当前选中项为 user_item 且已处于运行态
 */
extern bool xerintosh_is_in_user_item(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CORE_H */
