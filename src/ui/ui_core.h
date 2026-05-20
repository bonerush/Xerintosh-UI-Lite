#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALLOW_EXIT_ASTRA_UI_BY_USER 1

extern bool in_astra;

extern void ad_astra(void);
extern bool astra_is_in_user_item(void);
extern void astra_refresh_info_bar(void);
extern void astra_refresh_pop_up(void);
extern void astra_refresh_camera_position(void);
extern void astra_refresh_widget_core_position(void);
extern void astra_init_list(void);
extern void astra_init_core(void);
extern void astra_refresh_list_item_position(void);
extern void astra_refresh_selector_position(void);
extern void astra_refresh_main_core_position(void);
extern void astra_ui_widget_core(void);
extern void astra_ui_main_core(void);

extern void astra_animation(float *_pos, float _posTrg, float _speed);
extern uint8_t astra_exit_animation_status;

extern bool g_anim_enabled;

#ifdef __cplusplus
}
#endif

#endif
