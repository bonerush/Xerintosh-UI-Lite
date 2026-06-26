/**
 * @file   hal_display.h
 * @brief  HAL 显示层头文件
 * @details 定义屏幕尺寸常量、颜色常量、绘制原语、字体操作及裁剪接口。
 *          提供统一 API，底层在 native 环境使用内存帧缓冲，在硬件环境使用 LovyanGFX。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdint.h>
#include "hal_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 颜色常量 ═══ */

#define COLOR_BG      0x0000  /* 背景色：黑色 */
#define COLOR_FG      0xFFFF  /* 前景色：白色 */
#define COLOR_ACCENT  0x07E0  /* 强调色：绿色 */
#define COLOR_RED     0xF800  /* 红色：用于 Master 前缀 */

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化显示子系统
 */
extern void hal_display_init(void);

/**
 * @brief 反初始化显示子系统，释放 LovyanGFX 帧缓冲内存
 * @note  调用后所有绘制操作变为空操作（安全跳过）。
 *        可随后再次调用 hal_display_init() 重新创建。
 */
extern void hal_display_deinit(void);

/**
 * @brief 清屏（填充背景色）
 */
extern void hal_display_clear(void);

/**
 * @brief 清屏（填充指定颜色）
 * @param color 16 位 RGB565 颜色
 */
extern void hal_display_clear_color(uint16_t color);

/**
 * @brief 将离屏缓冲区内容刷新到物理屏幕
 */
extern void hal_display_flush(void);

/* ═══ 绘制原语 ═══ */

/**
 * @brief  绘制像素点
 * @param x     x 坐标
 * @param y     y 坐标
 * @param color 16 位 RGB565 颜色
 */
extern void hal_draw_pixel(int16_t x, int16_t y, uint16_t color);

/**
 * @brief  绘制线段（Bresenham 算法）
 * @param x1    起点 x
 * @param y1    起点 y
 * @param x2    终点 x
 * @param y2    终点 y
 * @param color 颜色
 */
extern void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

/**
 * @brief  绘制水平线段
 * @param x     起点 x
 * @param y     y 坐标
 * @param len   长度（可为负，表示向左绘制）
 * @param color 颜色
 */
extern void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color);

/**
 * @brief  绘制垂直线段
 * @param x     x 坐标
 * @param y     起点 y
 * @param len   长度（可为负，表示向上绘制）
 * @param color 颜色
 */
extern void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color);

/**
 * @brief  绘制空心矩形
 * @param x     左上角 x
 * @param y     左上角 y
 * @param w     宽度
 * @param h     高度
 * @param color 颜色
 */
extern void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief  绘制实心矩形
 * @param x     左上角 x
 * @param y     左上角 y
 * @param w     宽度
 * @param h     高度
 * @param color 颜色
 */
extern void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief  绘制空心圆角矩形
 * @param x     左上角 x
 * @param y     左上角 y
 * @param w     宽度
 * @param h     高度
 * @param r     圆角半径
 * @param color 颜色
 */
extern void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

/**
 * @brief  绘制实心圆角矩形
 * @param x     左上角 x
 * @param y     左上角 y
 * @param w     宽度
 * @param h     高度
 * @param r     圆角半径
 * @param color 颜色
 */
extern void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

/**
 * @brief  绘制空心圆（中点圆算法）
 * @param x     圆心 x
 * @param y     圆心 y
 * @param r     半径
 * @param color 颜色
 */
extern void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color);

/* ═══ 字体与文本 ═══ */

/**
 * @brief  设置当前字体
 * @param font 字体指针（硬件环境下为 lgfx::v1::IFont*）
 */
extern void hal_set_font(const void* font);

/**
 * @brief  绘制 ASCII 字符串
 * @param x     基线左对齐 x 坐标
 * @param y     基线 y 坐标
 * @param str   字符串
 * @param color 颜色
 */
extern void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color);

/**
 * @brief  绘制 UTF-8 字符串
 * @note   hal_draw_utf8 是 hal_draw_string 的别名（LovyanGFX drawString 本身即 UTF-8 兼容）
 */
#define hal_draw_utf8(x, y, str, color) hal_draw_string(x, y, str, color)

/**
 * @brief  获取 ASCII 字符串宽度（像素）
 * @param str 字符串
 * @return 宽度（像素）
 */
extern int16_t hal_get_string_width(const char* str);

/**
 * @brief  获取 UTF-8 字符串宽度（像素）
 * @note   hal_get_utf8_width 是 hal_get_string_width 的别名
 */
#define hal_get_utf8_width(str) hal_get_string_width(str)

/**
 * @brief  获取当前字体高度
 * @return 字体高度（像素）
 */
extern int16_t hal_get_font_height(void);

/**
 * @brief  获取中文字体指针
 * @return 中文字体指针
 */
extern const void* hal_get_cn_font(void);

/* ═══ 高级绘制 ═══ */

/**
 * @brief  绘制 XOR 反色矩形（用于选择器高亮）
 * @param x 左上角 x
 * @param y 左上角 y
 * @param w 宽度
 * @param h 高度
 */
extern void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief  绘制 XBM 位图
 * @param x      左上角 x
 * @param y      左上角 y
 * @param w      位图宽度
 * @param h      位图高度
 * @param bitmap 位图数据（每像素 1 bit，MSB 在前）
 */
extern void hal_draw_xbitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap);

/**
 * @brief  设置裁剪矩形，后续绘制操作仅限于此区域
 * @param x 左上角 x
 * @param y 左上角 y
 * @param w 宽度
 * @param h 高度
 */
extern void hal_set_clip_rect(int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief  清除裁剪矩形，恢复全屏绘制
 */
extern void hal_clear_clip_rect(void);

/* ═══ 显示配置 ═══ */

/**
 * @brief 设置屏幕方向
 * @param rotation 方向值（0-3），与 LovyanGFX setRotation 语义一致：
 *                 0=竖屏, 1=横屏, 2=反向竖屏, 3=反向横屏
 */
void hal_display_set_rotation(int rotation);

/**
 * @brief 获取当前屏幕方向
 * @return 当前方向值（0-3）
 */
int hal_display_get_rotation(void);

/**
 * @brief 设置屏幕背光亮度
 * @param level 亮度值（0-255）
 */
void hal_display_set_brightness(uint8_t level);

/**
 * @brief 获取当前屏幕背光亮度
 * @return 亮度值（0-255）
 */
uint8_t hal_display_get_brightness(void);

/* ═══ 测试钩子（仅 NATIVE_TEST） ═══ */

#ifdef NATIVE_TEST
/**
 * @brief  读取帧缓冲中指定坐标的像素值（测试验证用）
 * @param  x x 坐标
 * @param  y y 坐标
 * @return RGB565 颜色值；越界返回 0
 */
uint16_t hal_test_fb_read(int16_t x, int16_t y);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H */
