#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALLOW_EXIT_ASTRA_UI_BY_USER 1

/* ─── 全局状态 ─── */

extern bool in_xerintosh;
extern bool g_anim_enabled;
extern uint8_t xerintosh_exit_animation_status;

/* ─── 生命周期 ─── */

extern void xerintosh_init_core(void);
extern void xerintosh_init_list(void);

/* ─── 主循环 ─── */

extern void xerintosh_ui_main_core(void);
extern void xerintosh_ui_widget_core(void);

/* ─── 刷新位置 ─── */

extern void xerintosh_refresh_list_item_position(void);
extern void xerintosh_refresh_selector_position(void);
extern void xerintosh_refresh_main_core_position(void);
extern void xerintosh_refresh_camera_position(void);
extern void xerintosh_refresh_widget_core_position(void);
extern void xerintosh_refresh_info_bar(void);
extern void xerintosh_refresh_pop_up(void);

/* ─── 动画工具 ─── */

extern void xerintosh_animation(float *_pos, float _posTrg, float _speed);

/* ─── 状态查询 ─── */

extern bool xerintosh_is_in_user_item(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CORE_H */
