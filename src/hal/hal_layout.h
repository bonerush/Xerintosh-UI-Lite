/**
 * @file   hal_layout.h
 * @brief  HAL 级布局原语
 * @details 为所有 App 提供与屏幕尺寸无关的定位宏。三层层级：
 *          1. 原始边距常量（HAL_MARGIN_SM/MD/LG）
 *          2. 语义化尺寸（HAL_ROW_H）
 *          3. 区域边界与对齐（HAL_CENTER_X, HAL_HEADER_BOTTOM 等）
 *
 *          全部使用 #define 宏，C 兼容，零运行时开销。
 *          基于 SCREEN_WIDTH / SCREEN_HEIGHT / hal_get_font_height() 计算。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_LAYOUT_H
#define HAL_LAYOUT_H

#include "hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 层1: 原始边距常量 ═══ */

#define HAL_MARGIN_SM   ((int16_t)2)   /* 小边距：按钮 padding，分隔线间距 */
#define HAL_MARGIN_MD   ((int16_t)4)   /* 中边距：标准左右缩进 */
#define HAL_MARGIN_LG   ((int16_t)8)   /* 大边距：区块间间距 */

/* ═══ 层2: 语义化尺寸 ═══ */

/**
 * @brief 标准行高 = 字体高度 + 上下 padding
 * @note  用于 header / footer / 列表行
 */
#define HAL_ROW_H()     ((int16_t)(hal_get_font_height() + HAL_MARGIN_SM * 2))

/* ═══ 层3: 区域边界 ═══ */

#define HAL_HEADER_TOP()    ((int16_t)0)
#define HAL_HEADER_BOTTOM() ((int16_t)HAL_ROW_H())
#define HAL_FOOTER_TOP()    ((int16_t)(SCREEN_HEIGHT - HAL_ROW_H()))
#define HAL_FOOTER_BOTTOM() ((int16_t)SCREEN_HEIGHT)
#define HAL_BODY_TOP()      ((int16_t)HAL_HEADER_BOTTOM())
#define HAL_BODY_BOTTOM()   ((int16_t)HAL_FOOTER_TOP())
#define HAL_BODY_HEIGHT()   ((int16_t)(HAL_BODY_BOTTOM() - HAL_BODY_TOP()))

/* ═══ 层3: 对齐 ═══ */

/**
 * @brief 水平居中：返回使宽度为 w 的元素居中的 x 坐标
 */
#define HAL_CENTER_X(w)     ((int16_t)((SCREEN_WIDTH - (w)) / 2))

/**
 * @brief 垂直居中：返回使高度为 h 的元素居中的 y 坐标（顶部）
 */
#define HAL_CENTER_Y(h)     ((int16_t)((SCREEN_HEIGHT - (h)) / 2))

/**
 * @brief 左对齐（标准缩进 = HAL_MARGIN_MD）
 */
#define HAL_LEFT_X()        ((int16_t)HAL_MARGIN_MD)

/**
 * @brief 行内文字基线：在行顶部 y 处计算文字 baseline
 * @note  因为 hal_draw_string 使用 baseline 坐标系，文字 Y = row_top + font_height
 */
#define HAL_TEXT_BASELINE(row_top)  ((int16_t)(row_top) + hal_get_font_height())

#ifdef __cplusplus
}
#endif

#endif /* HAL_LAYOUT_H */
