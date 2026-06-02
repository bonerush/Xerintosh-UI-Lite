/**
 * @file   ui_widget.h
 * @brief  Xerintosh UI 控件头文件（信息栏 + 弹窗）
 * @details 定义顶部信息栏和中部弹窗的数据结构及操作接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_WIDGET_H
#define UI_WIDGET_H

#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 信息栏 ═══ */

#define INFO_BAR_HEIGHT 15
#define INFO_BAR_OFFSET 10

/**
 * @brief 顶部信息栏结构体
 * @note  y_info_bar 为当前位置，y_info_bar_trg 为目标位置
 */
typedef struct xerintosh_info_bar_t
{
  const char *content;       /* 显示文本 */
  uint16_t span;             /* 显示持续时间（毫秒） */
  float y_info_bar, y_info_bar_trg, w_info_bar, w_info_bar_trg;  /* 位置与宽度 */
  bool is_running;           /* 是否正在显示 */
  uint32_t time_start;       /* 开始显示的时间戳 */
  uint32_t time;             /* 最近一次更新的时间戳 */
} xerintosh_info_bar_t;

extern xerintosh_info_bar_t g_xerintosh_info_bar;

/**
 * @brief 推送顶部信息栏
 * @param _content 显示文本
 * @param _span    显示持续时间（毫秒）
 */
extern void xerintosh_push_info_bar(const char *_content, const uint16_t _span);

/* ═══ 弹窗 ═══ */

#define POP_UP_HEIGHT 48
#define POP_UP_OFFSET 8
#define POP_UP_WRAP_LINES 3

/**
 * @brief 中部弹窗结构体
 */
typedef struct xerintosh_pop_up_t
{
  const char *content;       /* 显示文本 */
  uint16_t span;             /* 显示持续时间（毫秒） */
  float y_pop_up, y_pop_up_trg, w_pop_up, w_pop_up_trg;  /* 位置与宽度 */
  bool is_running;           /* 是否正在显示 */
  uint32_t time_start;       /* 开始显示的时间戳 */
  uint32_t time;             /* 最近一次更新的时间戳 */
  const char *wrap_lines[POP_UP_WRAP_LINES];  /* 换行后的各行指针 */
  uint8_t wrap_line_count;   /* 实际行数 */
} xerintosh_pop_up_t;

extern xerintosh_pop_up_t g_xerintosh_pop_up;

/**
 * @brief 推送中部弹窗
 * @param _content 显示文本
 * @param _span    显示持续时间（毫秒）
 */
extern void xerintosh_push_pop_up(const char *_content, const uint16_t _span);

/**
 * @brief 立即隐藏弹窗（无动画，瞬间移出屏幕）
 */
extern void xerintosh_hide_pop_up(void);

/**
 * @brief 动画退出弹窗（触发向上滑出动画，动画结束后自动停止）
 */
extern void xerintosh_dismiss_pop_up(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_H */
