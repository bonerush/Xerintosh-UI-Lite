/**
 * @file   ui_dirty.c
 * @brief  Xerintosh UI 脏矩形管理实现
 * @details 封装 dirty region 的读写操作，提供统一的 invalidate / invalidate_region /
 *          is_dirty / get_dirty_region / clear_dirty API。
 *          内部通过 ui_context 单例访问 dirty_region 字段。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_dirty.h"
#include "ui_context.h"
#include "hal/hal_display.h"

/* ═══ 公开 API 实现 ═══ */

/**
 * @brief 标记 UI 需要重绘（全屏区域）
 */
void xerintosh_invalidate(void)
{
    xerintosh_dirty_region_t *r = &xerintosh_get_context()->dirty_region;
    r->active = true;
    r->x = 0;
    r->y = 0;
    r->w = HAL_SCREEN_WIDTH;
    r->h = HAL_SCREEN_HEIGHT;
}

/**
 * @brief 标记指定屏幕区域为脏状态
 */
void xerintosh_invalidate_region(int16_t x, int16_t y, int16_t w, int16_t h)
{
    xerintosh_dirty_region_t *r = &xerintosh_get_context()->dirty_region;

    if (!r->active) {
        r->x = x;
        r->y = y;
        r->w = w;
        r->h = h;
    } else {
        int16_t x1 = r->x;
        int16_t y1 = r->y;
        int16_t x2 = x1 + r->w;
        int16_t y2 = y1 + r->h;
        int16_t nx2 = x + w;
        int16_t ny2 = y + h;

        if (x < x1) x1 = x;
        if (y < y1) y1 = y;
        if (nx2 > x2) x2 = nx2;
        if (ny2 > y2) y2 = ny2;

        /* 合并后的区域接近全屏(>=90%)时直接升级为全屏脏矩形，
         * 避免多次小区域合并后性能退化 */
        int32_t merged_w = x2 - x1;
        int32_t merged_h = y2 - y1;
        if (merged_w * merged_h >= (int32_t)(HAL_SCREEN_WIDTH * HAL_SCREEN_HEIGHT) * 90 / 100) {
            r->x = 0;
            r->y = 0;
            r->w = HAL_SCREEN_WIDTH;
            r->h = HAL_SCREEN_HEIGHT;
            r->active = true;
            return;
        }

        r->x = x1;
        r->y = y1;
        r->w = (int16_t)merged_w;
        r->h = (int16_t)merged_h;
    }

    r->active = true;
}

/**
 * @brief 查询是否需要重绘
 */
bool xerintosh_is_dirty(void)
{
    xerintosh_dirty_region_t *r = &xerintosh_get_context()->dirty_region;
    if (!r->active) return false;
    /* 无效的脏矩形尺寸（w <= 0 或 h <= 0）视作未脏 */
    if (r->w <= 0 || r->h <= 0) return false;
    return true;
}

/**
 * @brief 获取当前脏矩形区域（只读）
 */
const xerintosh_dirty_region_t *xerintosh_get_dirty_region(void)
{
    return &xerintosh_get_context()->dirty_region;
}

/**
 * @brief 清除脏标志（渲染完成后调用）
 */
void xerintosh_clear_dirty(void)
{
    xerintosh_get_context()->dirty_region.active = false;
}
