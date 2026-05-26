/**
 * @file   ui_item_popup.c
 * @brief  信息栏与弹窗管理
 * @details 实现顶部信息栏和中部弹窗的推送、隐藏及字体设置。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"
#include "ui_core.h"
#include "hal/hal_system.h"
#include <stddef.h>
#include <string.h>

/* ═══ 字体 ═══ */

static const void *g_xerintosh_font = NULL;  /* 当前字体指针 */

/**
 * @brief 设置当前绘图字体（仅当字体变化时才更新 HAL）
 * @param _font 字体指针
 */
void xerintosh_set_font(const void *_font)
{
  if (_font != g_xerintosh_font) hal_set_font(_font);
}

/* ═══ 信息栏 ═══ */

xerintosh_info_bar_t g_xerintosh_info_bar = {0, 1, 0 - 2 * INFO_BAR_HEIGHT, 0 - 2 * INFO_BAR_HEIGHT, 80, 80, false, 0, 1};

/**
 * @brief 推送顶部信息栏
 * @param _content 显示文本
 * @param _span    显示持续时间（毫秒）
 * @note   如果信息栏正在显示相同内容，则重置计时器；否则重新展开
 */
void xerintosh_push_info_bar(const char *_content, const uint16_t _span)
{
  /* 设定显示时间的概念：超过了显示时间，就将 y_trg 设为初始位置；
     如果在显示时间之内有新的消息涌入，则 y 和 y_trg 都不变，继续显示，且显示时间清零。
     只有显示时间到了的时候，才会复位。 */

  g_xerintosh_info_bar.time = hal_get_ticks();
  g_xerintosh_info_bar.content = _content;
  g_xerintosh_info_bar.span = _span;
  g_xerintosh_info_bar.is_running = false; /* 每次进入该函数都代表有新的消息涌入，所以需要重置 is_running */

  /* 展开弹窗；收回弹窗和同步时间戳需要在循环中进行，所以移到了 drawer 中 */
  if (!g_xerintosh_info_bar.is_running)
  {
    g_xerintosh_info_bar.time_start = hal_get_ticks();
    g_xerintosh_info_bar.y_info_bar_trg = 0;
    g_xerintosh_info_bar.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_info_bar.w_info_bar_trg = hal_get_utf8_width(g_xerintosh_info_bar.content) + INFO_BAR_OFFSET;
}

/* ═══ 弹窗 ═══ */

xerintosh_pop_up_t g_xerintosh_pop_up = {0, 1, 0 - 2 * POP_UP_HEIGHT, 0 - 2 * POP_UP_HEIGHT, 80, 80, false, 0, 1};

/**
 * @brief 推送中部弹窗
 * @param _content 显示文本
 * @param _span    显示持续时间（毫秒）
 * @note   如果弹窗正在显示相同内容，则重置计时器并提升位置
 */
void xerintosh_push_pop_up(const char *_content, const uint16_t _span)
{
  if (g_xerintosh_pop_up.is_running && g_xerintosh_pop_up.content != NULL
      && strcmp(g_xerintosh_pop_up.content, _content) == 0) {
    g_xerintosh_pop_up.time_start = hal_get_ticks();
    g_xerintosh_pop_up.span = _span;
    g_xerintosh_pop_up.y_pop_up_trg = 20;
    return;
  }

  g_xerintosh_pop_up.time = hal_get_ticks();
  g_xerintosh_pop_up.content = _content;
  g_xerintosh_pop_up.span = _span;
  g_xerintosh_pop_up.is_running = false;

  /* 弹出 */
  if (!g_xerintosh_pop_up.is_running)
  {
    g_xerintosh_pop_up.time_start = hal_get_ticks();
    g_xerintosh_pop_up.y_pop_up_trg = 20;
    g_xerintosh_pop_up.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_pop_up.w_pop_up_trg = hal_get_utf8_width(g_xerintosh_pop_up.content) + POP_UP_OFFSET;
}

/**
 * @brief 立即隐藏弹窗（将位置重置到屏幕外）
 */
void xerintosh_hide_pop_up(void)
{
  g_xerintosh_pop_up.is_running = false;
  g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
  g_xerintosh_pop_up.y_pop_up = 0 - 2 * POP_UP_HEIGHT;
}
