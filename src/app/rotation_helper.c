/**
 * @file   rotation_helper.c
 * @brief  屏幕方向值兼容性转换辅助函数
 * @details 将旧的 GFX rotation 值（0/1/2/3）映射到新的等级值（1/2）
 *
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include "settings.h"

/**
 * @brief  将存储中读取的旋转值解析为新的等级格式
 * @param  saved_rot 从存储读取的原始值
 * @return 1 竖屏 (ORIENTATION_PORTRAIT)
 * @return 2 横屏 (ORIENTATION_LANDSCAPE)
 * @note   兼容性映射：
 *         - 旧 0° 竖屏 (0)   → 新 1
 *         - 旧 90° 横屏 (1)  → 新 2
 *         - 旧 180° 竖屏 (2) → 新 1
 *         - 旧 270° 横屏 (3) → 新 2
 *         - 无效值           → 新 2（默认横屏）
 */
int16_t resolve_rotation_level(uint8_t saved_rot)
{
    switch (saved_rot) {
        case 0: /* 旧 0° 竖屏 */
        case 2: /* 旧 180° 竖屏 */
            return ORIENTATION_PORTRAIT;
        case 1: /* 旧 90° 横屏 */
        case 3: /* 旧 270° 横屏 */
            return ORIENTATION_LANDSCAPE;
        default:
            /* 无效值默认横屏 */
            return ORIENTATION_LANDSCAPE;
    }
}
