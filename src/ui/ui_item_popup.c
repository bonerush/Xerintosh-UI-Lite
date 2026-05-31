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

/* 弹窗文字换行缓冲区（独立缓冲区，不做原地修改） */
static char g_wrap_line0[48];
static char g_wrap_line1[48];
static char g_wrap_line2[48];

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

/* ═══ 换行辅助 ═══ */

/**
 * @brief 在指定可用宽度内寻找最佳断行点（优先标点）
 * @param text  文本指针
 * @param len   文本字节长度
 * @param avail 可用宽度（像素，估算值）
 * @return 断行位置（字节偏移），0 表示未找到
 */
static size_t find_wrap_break(const char *text, size_t len, int16_t avail)
{
  int16_t acc = 0;
  size_t p = 0;
  size_t best = 0;
  size_t best_punct = 0;

  while (p < len)
  {
    unsigned char c = (unsigned char)text[p];
    size_t clen = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (p + clen > len) clen = len - p;

    int16_t cw = (clen == 1) ? ((c >= 0x20 && c < 0x7F) ? 7 : 0) : 12;

    if (acc + cw > avail) break;

    acc += cw;
    p += clen;

    best = p;

    if (clen == 1 && (c == ',' || c == '.' || c == ' ' ||
                      c == ';' || c == '-' || c == '!' || c == '?'))
      best_punct = p;
  }

  if (best_punct > len / 3)
    best = best_punct;

  return best;
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
  g_xerintosh_info_bar.w_info_bar_trg = hal_get_string_width(g_xerintosh_info_bar.content) + INFO_BAR_OFFSET;
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
  /* 仅当指针不同且内容相同时才跳过重算
     （指针相同说明调用方复用了缓冲区，内容可能已变） */
  if (g_xerintosh_pop_up.is_running && g_xerintosh_pop_up.content != NULL
      && _content != g_xerintosh_pop_up.content
      && strcmp(g_xerintosh_pop_up.content, _content) == 0) {
    g_xerintosh_pop_up.time_start = hal_get_ticks();
    g_xerintosh_pop_up.span = _span;
    g_xerintosh_pop_up.y_pop_up_trg = (SCREEN_HEIGHT - POP_UP_HEIGHT) / 2;
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
    g_xerintosh_pop_up.y_pop_up_trg = (SCREEN_HEIGHT - POP_UP_HEIGHT) / 2;
    g_xerintosh_pop_up.is_running = true;
  }

  xerintosh_set_font(hal_get_cn_font());

  /* ── 自动换行：当文字超出可用宽度时拆为多行 ── */
  {
    int16_t max_w = SCREEN_WIDTH - 12;   /* 弹窗宽度上限（为边框留 12px） */
    int16_t avail = max_w - POP_UP_OFFSET;  /* 文字可用宽度 */
    int16_t text_w = hal_get_string_width(_content);

    if (avail < 20) avail = 20;

    if (text_w > avail && POP_UP_WRAP_LINES >= 2)
    {
      size_t len = strlen(_content);
      size_t best1 = find_wrap_break(_content, len, avail);

      if (best1 > 0 && best1 < len)
      {
        xerintosh_set_font(hal_get_cn_font());

        /* 尝试 3 行：在第二段中再次寻找断行点 */
        if (POP_UP_WRAP_LINES >= 3)
        {
          size_t best2_rel = find_wrap_break(_content + best1, len - best1, avail);
          if (best2_rel > 0 && best2_rel < len - best1)
          {
            size_t best2 = best1 + best2_rel;
            size_t l0 = best1, l1 = best2_rel, l2 = len - best2;
            if (l0 >= sizeof(g_wrap_line0)) l0 = sizeof(g_wrap_line0) - 1;
            if (l1 >= sizeof(g_wrap_line1)) l1 = sizeof(g_wrap_line1) - 1;
            if (l2 >= sizeof(g_wrap_line2)) l2 = sizeof(g_wrap_line2) - 1;
            memcpy(g_wrap_line0, _content, l0);          g_wrap_line0[l0] = '\0';
            memcpy(g_wrap_line1, _content + best1, l1);  g_wrap_line1[l1] = '\0';
            memcpy(g_wrap_line2, _content + best2, l2);  g_wrap_line2[l2] = '\0';

            g_xerintosh_pop_up.wrap_lines[0] = g_wrap_line0;
            g_xerintosh_pop_up.wrap_lines[1] = g_wrap_line1;
            g_xerintosh_pop_up.wrap_lines[2] = g_wrap_line2;
            g_xerintosh_pop_up.wrap_line_count = 3;

            int16_t w0 = hal_get_string_width(g_wrap_line0);
            int16_t w1 = hal_get_string_width(g_wrap_line1);
            int16_t w2 = hal_get_string_width(g_wrap_line2);
            int16_t wmax = (w0 > w1) ? ((w0 > w2) ? w0 : w2) : ((w1 > w2) ? w1 : w2);
            g_xerintosh_pop_up.w_pop_up_trg = wmax + POP_UP_OFFSET;
            if ((int16_t)g_xerintosh_pop_up.w_pop_up_trg > max_w)
              g_xerintosh_pop_up.w_pop_up_trg = max_w;
            return;
          }
        }

        /* 2 行 */
        {
          size_t l0 = best1, l1 = len - best1;
          if (l0 >= sizeof(g_wrap_line0)) l0 = sizeof(g_wrap_line0) - 1;
          if (l1 >= sizeof(g_wrap_line1)) l1 = sizeof(g_wrap_line1) - 1;
          memcpy(g_wrap_line0, _content, l0);
          g_wrap_line0[l0] = '\0';
          memcpy(g_wrap_line1, _content + best1, l1);
          g_wrap_line1[l1] = '\0';

          g_xerintosh_pop_up.wrap_lines[0] = g_wrap_line0;
          g_xerintosh_pop_up.wrap_lines[1] = g_wrap_line1;
          g_xerintosh_pop_up.wrap_line_count = 2;

          int16_t w0 = hal_get_string_width(g_wrap_line0);
          int16_t w1 = hal_get_string_width(g_wrap_line1);
          g_xerintosh_pop_up.w_pop_up_trg = ((w0 > w1) ? w0 : w1) + POP_UP_OFFSET;
          if ((int16_t)g_xerintosh_pop_up.w_pop_up_trg > max_w)
            g_xerintosh_pop_up.w_pop_up_trg = max_w;
          return;
        }
      }
    }
  }

  /* 单行模式（横屏或短文本） */
  g_xerintosh_pop_up.wrap_lines[0] = _content;
  g_xerintosh_pop_up.wrap_line_count = 1;
  g_xerintosh_pop_up.w_pop_up_trg = hal_get_string_width(_content) + POP_UP_OFFSET;
  if ((int16_t)g_xerintosh_pop_up.w_pop_up_trg > SCREEN_WIDTH - 12)
    g_xerintosh_pop_up.w_pop_up_trg = SCREEN_WIDTH - 12;
}

/**
 * @brief 立即隐藏弹窗（无动画，瞬间移出屏幕）
 */
void xerintosh_hide_pop_up(void)
{
  g_xerintosh_pop_up.is_running = false;
  g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
  g_xerintosh_pop_up.y_pop_up = 0 - 2 * POP_UP_HEIGHT;
}

/**
 * @brief 动画退出弹窗（触发向上滑出动画，动画结束后自动停止）
 */
void xerintosh_dismiss_pop_up(void)
{
  if (!g_xerintosh_pop_up.is_running) return;
  g_xerintosh_pop_up.y_pop_up_trg = 0 - 2 * POP_UP_HEIGHT;
}
