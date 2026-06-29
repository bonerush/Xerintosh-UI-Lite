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

/* 全局上下文（替代分散的全局变量） */
#include "ui/ui_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 UI 核心状态（列表、选择器、相机绑定）
 */
extern void xerintosh_init_core(void);

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

/* ═══ 动画工具 ═══ */

/**
 * @brief  通用缓动动画函数
 * @param  _pos     当前位置指针（会被直接更新）
 * @param  _pos_trg 目标位置
 * @param  _speed   动画速度（自动裁剪到 ANIM_SPEED_MIN..ANIM_SPEED_MAX）
 * @return true  位置已稳定在目标（已到位或已吸附）
 * @return false 动画仍在进行中
 * @note   公式：current += (target - current) / (100 - speed)
 * @note   当 g_anim_enabled 为 false 时直接跳转到目标位置。
 * @note   diff <= ANIM_SNAP_THRESHOLD (1.0f) 时直接吸附到目标。
 */
extern bool xerintosh_animation(float *_pos, float _pos_trg, float _speed);

/**
 * @brief  弹簧动画函数（二阶欠阻尼系统）
 * @param  _pos      当前位置指针（会被直接更新）
 * @param  _vel      当前速度指针（会被直接更新）
 * @param  _pos_trg  目标位置
 * @param  _stiffness 刚度系数（越大回弹越快，典型 0.05-0.20）
 * @param  _damping  粘性阻尼系数（越大衰减越快，典型 0.20-0.50）
 * @note   基于自动控制理论二阶弹簧-阻尼器模型（additive damping）：
 *         force = k*(target-x) - c*v; v += force; x += v
 * @note   当 stiffness=0.10, damping=0.30 时 ζ≈0.47，产生约18%超调弹性效果
 * @note   当 g_anim_enabled 为 false 时直接跳转到目标位置
 * @note   靠近目标(<0.5px)且速度足够小(<0.5px/frame)时自动吸附
 */
extern void xerintosh_spring_animation(float *_pos, float *_vel, float _pos_trg,
                                        float _stiffness, float _damping);

/**
 * @brief  统一缓动动画调度（已移除弹簧模式分支）
 * @param  _pos     当前位置指针（会被直接更新）
 * @param  _vel     当前速度指针（保留，但不再使用；弹簧模式调用方直接调用 spring）
 * @param  _target  目标位置
 * @param  _speed   动画速度（0~99）
 * @return true  已稳定在目标 / false 动画进行中
 * @note   g_spring_anim_mode 分支已从此函数移除，调用方直接选择动画类型。
 */
extern bool xerintosh_animate_unified(float *_pos, float *_vel, float _target, float _speed);

/* ═══ 状态查询 ═══ */

/**
 * @brief  查询当前是否处于 user_item 内部
 * @return true  当前选中项为 user_item 且已处于运行态
 */
extern bool xerintosh_is_in_user_item(void);

/**
 * @brief  注册双键按住检测回调
 * @param  cb 返回 true 表示当前处于双键模式，UI 退场动画应被跳过
 * @note   由 App 层（如 power_key_popup）注册，解除 UI 核心对 App 的反向依赖
 */
extern void xerintosh_set_dual_key_callback(bool (*cb)(void));

/**
 * @brief  标记 UI 为脏状态，请求下一帧全量重绘
 * @note   user_item 内部不需要调用（框架每帧自动清屏）。
 *         菜单模式下非动画触发状态变化时调用。
 */
extern void xerintosh_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CORE_H */
