#ifndef UI_DRAWER_H
#define UI_DRAWER_H

#include "ui_item.h"

extern uint8_t astra_exit_animation_status;

extern void astra_draw_exit_animation(void);
extern void astra_draw_info_bar(void);
extern void astra_draw_pop_up(void);
extern void astra_draw_list_appearance(void);
extern void astra_draw_list_item(void);
extern void astra_draw_list_icon(astra_list_item_icon_t icon, uint16_t x, uint16_t y);
extern void astra_draw_selector(void);
extern void astra_draw_widget(void);
extern void astra_draw_list(void);

#endif
