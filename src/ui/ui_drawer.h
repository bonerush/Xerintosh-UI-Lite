#ifndef UI_DRAWER_H
#define UI_DRAWER_H

#include "ui_item.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 全局状态 ─── */

extern uint8_t xerintosh_exit_animation_status;

/* ─── 绘制函数 ─── */

extern void xerintosh_draw_exit_animation(void);
extern void xerintosh_draw_info_bar(void);
extern void xerintosh_draw_pop_up(void);
extern void xerintosh_draw_list_appearance(void);
extern void xerintosh_draw_list_item(void);
extern void xerintosh_draw_list_icon(xerintosh_list_item_icon_t icon, uint16_t x, uint16_t y);
extern void xerintosh_draw_selector(void);
extern void xerintosh_draw_widget(void);
extern void xerintosh_draw_long_press_hint(uint32_t duration_ms, uint32_t threshold_ms);
extern void xerintosh_draw_list(void);

/* ─── 文字滚动工具 ─── */

extern float xerintosh_compute_scroll_offset(int16_t text_width, int16_t avail_width,
                                          bool is_selected, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* UI_DRAWER_H */
