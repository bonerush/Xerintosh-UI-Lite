/**
 * @file   ui_dirty.h
 * @brief  Xerintosh UI 脏矩形（Dirty Region）管理接口
 * @details 提供统一的局部刷新/脏矩形标记 API，供 App 开发者和框架内部使用。
 *
 *          两个核心概念：
 *          - invalidate：标记 UI 需要重绘（设置 dirty flag）
 *          - clear：重绘完成后清除 dirty flag（框架内部调用）
 *
 *          开发者契约：
 *          - user_item 内部：框架每帧自动清屏，**不需要**调用 invalidate
 *          - 菜单模式下：状态变化（网络回调、计时器等）调用 xerintosh_invalidate()
 *          - 框架自动 invalidate 的场景：按键导航、选择器移动、动画播放、生命周期变更
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_DIRTY_H
#define UI_DIRTY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 公开 API ═══ */

/**
 * @brief  标记 UI 为脏状态，请求下一帧全量重绘
 * @note   App 开发者在菜单模式下需要即时更新 UI 时调用此函数。
 *         user_item 内部不需要调用（框架每帧自动清屏）。
 *         框架内部在按键导航、选择器移动、动画播放、生命周期变更时自动调用。
 *
 *         使用示例（网络状态回调中）：
 *         @code
 *         void on_status_changed(void *ud) {
 *             update_icon();
 *             xerintosh_invalidate();  // 强制下一帧重绘
 *         }
 *         @endcode
 */
void xerintosh_invalidate(void);

/**
 * @brief  查询当前是否处于脏状态（是否需要重绘）
 * @return true  需要重绘
 * @return false UI 干净，可跳过重绘
 * @note   框架内部用于脏矩形优化判断。App 开发者通常不需要调用。
 */
bool xerintosh_is_dirty(void);

/**
 * @brief  清除脏状态标志（重绘完成后由框架调用）
 * @note   仅在 xerintosh_ui_render_frame() 完成后调用。
 *         外部代码不应直接调用，请使用 xerintosh_invalidate() 标记脏。
 */
void xerintosh_clear_dirty(void);

/* ═══ 向后兼容（已弃用，新代码请使用 xerintosh_invalidate()）═══ */

/**
 * @deprecated 使用 xerintosh_invalidate() 替代
 */
void xerintosh_mark_dirty(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DIRTY_H */
