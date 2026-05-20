#ifndef UI_ITEM_H
#define UI_ITEM_H

#include "ui_draw_driver.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 动画速度常量 — 基于可配置的全局变量 */
extern int16_t g_anim_speed;

#define ANIM_SPEED_LIST_ITEM    (g_anim_speed - 8)
#define ANIM_SPEED_SELECTOR     (g_anim_speed)
#define ANIM_SPEED_SELECTOR_H   (g_anim_speed + 1)
#define ANIM_SPEED_INFO_BAR     (g_anim_speed + 2)
#define ANIM_SPEED_INFO_BAR_W   (g_anim_speed + 3)
#define ANIM_SPEED_POP_UP_W     (g_anim_speed + 4)
#define ANIM_SPEED_POP_UP_Y     (g_anim_speed + 2)
#define ANIM_SPEED_CAMERA       (g_anim_speed + 4)
#define ANIM_SPEED_EXIT         (g_anim_speed + 2)

static const void* astra_font;
extern void astra_set_font(const void* _font);

extern bool astra_exit_animation_finished;
extern bool astra_refresh_list_value;

/*** 信息栏 ***/
#define INFO_BAR_HEIGHT 15
#define INFO_BAR_OFFSET 10

typedef struct astra_info_bar_t
{
  const char *content;
  uint16_t span;
  float y_info_bar, y_info_bar_trg, w_info_bar, w_info_bar_trg;
  bool is_running;
  uint32_t time_start;
  uint32_t time;
} astra_info_bar_t;

extern astra_info_bar_t astra_info_bar;

extern void astra_push_info_bar(const char *_content, const uint16_t _span);
/*** 信息栏 ***/

/*** 弹窗 ***/
#define POP_UP_HEIGHT 20
#define POP_UP_OFFSET 8

typedef struct astra_pop_up_t
{
  const char *content;
  uint16_t span;
  float y_pop_up, y_pop_up_trg, w_pop_up, w_pop_up_trg;
  bool is_running;
  uint32_t time_start;
  uint32_t time;
} astra_pop_up_t;

extern astra_pop_up_t astra_pop_up;

extern void astra_push_pop_up(const char *_content, const uint16_t _span);
extern void astra_hide_pop_up(void);
/*** 弹窗 ***/

/*** 列表项 ***/
#define MAX_LIST_CHILD_NUM 10
#define MAX_LIST_LAYER 10
#define LIST_ITEM_SPACING 18
#define LIST_ITEM_OFFSET 8
#define LIST_ITEM_LEFT_MARGIN 4
#define LIST_ITEM_RIGHT_MARGIN 20
#define LIST_INFO_BAR_HEIGHT 3
#define LIST_FONT_TOP_MARGIN 6

typedef enum
{
  list_item,
  switch_item,
  slider_item,
  user_item,
  button_item,
} astra_list_item_type_t;

typedef enum {
    default_icon,
    list_icon,
    switch_icon,
    plus_icon,
    user_icon,
    slider_icon,
    flag_icon,
    power_icon,
} astra_list_item_icon_t;

typedef struct astra_list_item_t
{
  astra_list_item_type_t type;
  astra_list_item_icon_t icon;
  const char *content;

  uint8_t layer;
  float y_list_item, y_list_item_trg;
  uint8_t child_num;
  struct astra_list_item_t *child_list_item[MAX_LIST_CHILD_NUM];
  struct astra_list_item_t *parent;
  void *user_data;

  /* 文字滚动状态（Phase 1.2） */
  float content_scroll_offset;      /* 当前滚动偏移 */
  uint32_t scroll_start_time;       /* 滚动开始时间戳 */
  bool is_scrolling;                /* 是否正在滚动 */
} astra_list_item_t;

typedef struct astra_switch_item_t
{
  astra_list_item_t base_item;

  bool *value;
  void (*init_function)();
  void (*exit_function)();
} astra_switch_item_t;

typedef struct astra_button_item_t
{
  astra_list_item_t base_item;

  void (*exit_function)();
} astra_button_item_t;

typedef struct astra_slider_item_t
{
  astra_list_item_t base_item;

  int16_t *value;
  int16_t value_backup;
  bool is_confirmed;
  uint8_t value_step;
  int16_t value_max;
  int16_t value_min;
  void (*init_function)();
  void (*exit_function)();
} astra_slider_item_t;

typedef struct astra_user_item_t
{
  astra_list_item_t base_item;

  bool in_user_item;
  bool entering_user_item;
  bool exiting_user_item;
  void (*init_function)();
  void (*loop_function)();  //user_item的逻辑和item写在一起 方便渲染
  void (*exit_function)();
  bool user_item_inited;
  bool user_item_looping;
} astra_user_item_t;

extern astra_list_item_t *astra_get_root_list();

extern astra_switch_item_t *astra_to_switch_item(astra_list_item_t *_astra_list_item);
extern astra_button_item_t *astra_to_button_item(astra_list_item_t *_astra_list_item);
extern astra_slider_item_t *astra_to_slider_item(astra_list_item_t *_astra_list_item);
extern astra_user_item_t *astra_to_user_item(astra_list_item_t *_astra_list_item);
extern astra_list_item_t *astra_new_list_item(const char *_content, astra_list_item_icon_t icon);
//正确用法：astra_push_item_to_list(astra_get_root_list(), astra_new_list_item(...));
extern astra_list_item_t *astra_new_switch_item(const char *_content, bool *_value, void (*_init_function)(), void (*_exit_function)(), astra_list_item_icon_t icon);
extern astra_list_item_t *astra_new_button_item(const char *_content, void (*_exit_function)(), astra_list_item_icon_t icon);
extern astra_list_item_t *astra_new_slider_item(const char *_content, int16_t *_value, uint8_t _step, int16_t _min, int16_t _max, void (*_init_function)(), void (*_exit_function)(), astra_list_item_icon_t icon);
extern astra_list_item_t *astra_new_user_item(const char *_content, void (*_init_function)(), void (*_loop_function)(), void (*_exit_function)(), astra_list_item_icon_t icon);
//正确用法：astra_push_item_to_list(astra_get_root_list(), astra_new_user_item(...));

//此种方法合理且安全，本质是将user item类转换为了基类，用于渲染
//在此过程中，派生类的专有变量不会丢失内容，selector发现是user type后再转换回派生类执行对应内部函数即可

extern bool astra_push_item_to_list(astra_list_item_t *_parent, astra_list_item_t *_child);

// 从父列表中移除子项并释放内存
extern bool astra_remove_item_from_list(astra_list_item_t *_parent, astra_list_item_t *_child);
// 清空父列表的所有子项并释放内存
extern void astra_clear_children_of_list(astra_list_item_t *_parent);
/*** 列表项 ***/

/*** 选择器 ***/
typedef struct astra_selector_t
{
  float y_selector, y_selector_trg, w_selector, w_selector_trg, h_selector, h_selector_trg;
  uint8_t selected_index;
  astra_list_item_t *selected_item;
} astra_selector_t;

extern astra_selector_t astra_selector;
extern astra_selector_t* astra_get_selector();
extern bool astra_bind_item_to_selector(astra_list_item_t *_item);
extern void astra_selector_go_next_item();
extern void astra_selector_go_prev_item();
extern void astra_selector_jump_to_selected_item();
extern void astra_selector_exit_current_item();
/*** 选择器 ***/

/*** 相机 ***/
typedef struct astra_camera_t
{
  float x_camera, x_camera_trg, y_camera, y_camera_trg;
  astra_selector_t *selector;
} astra_camera_t;

extern astra_camera_t astra_camera;
extern astra_camera_t* astra_get_camera();
extern void astra_bind_selector_to_camera(astra_selector_t *_selector);
/*** 相机 ***/

#ifdef __cplusplus
}
#endif

#endif // UI_ITEM_H
