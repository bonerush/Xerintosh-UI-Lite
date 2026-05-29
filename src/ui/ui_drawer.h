/**
 * @file   ui_drawer.h
 * @brief  UI 渲染管线头文件
 * @details 定义列表外观、列表项、选择器、信息栏、弹窗、
 *          长按提示及文字滚动等全部绘制函数的接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_DRAWER_H
#define UI_DRAWER_H

#include "ui_item.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 绘制函数 ═══ */

/**
 * @brief 绘制退场遮罩动画（沙漏 + 扫描线效果）
 */
extern void xerintosh_draw_exit_animation(void);

/**
 * @brief 绘制顶部信息栏
 */
extern void xerintosh_draw_info_bar(void);

/**
 * @brief 绘制中部弹窗
 */
extern void xerintosh_draw_pop_up(void);

/**
 * @brief 绘制列表整体外观（边框、滚动条、装饰像素）
 */
extern void xerintosh_draw_list_appearance(void);

/**
 * @brief 绘制当前可见的所有列表项（含图标、文字、滚动效果）
 */
extern void xerintosh_draw_list_item(void);

/**
 * @brief 绘制指定类型的图标
 * @param icon 图标类型
 * @param x    图标左上角 x 坐标
 * @param y    图标中心 y 坐标
 */
extern void xerintosh_draw_list_icon(xerintosh_list_item_icon_t icon, uint16_t x, uint16_t y);

/**
 * @brief  绘制列表项的自定义位图图标
 * @param  _item 列表项指针（需已设置 bitmap_data）
 * @param  x     图标左上角 x 坐标
 * @param  y     图标中心 y 坐标
 * @note   当 icon 为 custom_icon 时由 draw_list_item_xxx 调用
 */
extern void xerintosh_draw_item_bitmap(xerintosh_list_item_t *_item, uint16_t x, uint16_t y);

/**
 * @brief 绘制选择器高亮框（XOR 反色矩形 + 右侧虚线装饰）
 */
extern void xerintosh_draw_selector(void);

/**
 * @brief  绘制长按进度提示条
 * @param  duration_ms  当前已长按的毫秒数
 * @param  threshold_ms 触发长按所需的毫秒数
 */
extern void xerintosh_draw_long_press_hint(uint32_t duration_ms, uint32_t threshold_ms);

/**
 * @brief 绘制完整列表帧（外观 + 列表项 + 选择器 + 滑块覆盖层）
 */
extern void xerintosh_draw_list(void);

/* ═══ 文字滚动工具 ═══ */

/**
 * @brief  计算文字循环滚动偏移量（纯函数，便于单元测试）
 * @param  text_width   文字总宽度（像素）
 * @param  avail_width  可用显示宽度（像素）
 * @param  is_selected  当前项是否被选中（仅选中项滚动）
 * @param  elapsed_ms   从选中开始经过的毫秒数
 * @return 水平偏移量（正值表示向左滚动）；无需滚动时返回 0
 */
extern float xerintosh_compute_scroll_offset(int16_t text_width, int16_t avail_width,
                                          bool is_selected, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* UI_DRAWER_H */
