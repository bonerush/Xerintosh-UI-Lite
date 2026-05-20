#ifndef UI_DRAWER_H
#define UI_DRAWER_H

#include "ui_item.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t astra_exit_animation_status;

extern void astra_draw_exit_animation(void);
extern void astra_draw_info_bar(void);
extern void astra_draw_pop_up(void);
extern void astra_draw_list_appearance(void);
extern void astra_draw_list_item(void);
extern void astra_draw_list_icon(astra_list_item_icon_t icon, uint16_t x, uint16_t y);
extern void astra_draw_selector(void);
extern void astra_draw_widget(void);
extern void astra_draw_long_press_hint(uint32_t duration_ms, uint32_t threshold_ms);
extern void astra_draw_list(void);

/* 文字滚动偏移计算（纯函数，可独立测试） */
extern float astra_compute_scroll_offset(int16_t text_width, int16_t avail_width,
                                          bool is_selected, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif
