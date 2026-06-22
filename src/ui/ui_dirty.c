/**
 * @file   ui_dirty.c
 * @brief  Xerintosh UI 脏矩形管理实现
 * @details 封装 dirty flag 的读写操作，提供统一的 invalidate / is_dirty / clear_dirty 三元 API。
 *          内部通过 ui_context 单例访问 dirty 字段。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_dirty.h"
#include "ui_context.h"

/* ═══ 公开 API 实现 ═══ */

/**
 * @brief 标记 UI 需要重绘
 */
void xerintosh_invalidate(void)
{
    xerintosh_get_context()->dirty = true;
}

/**
 * @brief 查询是否需要重绘
 */
bool xerintosh_is_dirty(void)
{
    return xerintosh_get_context()->dirty;
}

/**
 * @brief 清除脏标志（渲染完成后调用）
 */
void xerintosh_clear_dirty(void)
{
    xerintosh_get_context()->dirty = false;
}
