/**
 * @file   ui_item.h
 * @brief  Xerintosh UI 菜单项系统头文件
 * @details 定义五种菜单项类型（list/switch/slider/button/user）、
 *          选择器、相机、信息栏、弹窗等核心数据结构及操作接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_ITEM_H
#define UI_ITEM_H

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include <stdint.h>
#include <stdbool.h>

/* 内核 PID 类型（来自 kern_types.h，避免重复定义） */
#include "kernel/kern_types.h"

/* 全局上下文（替代分散的全局变量） */
#include "ui/ui_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 动画速度常量 ═══ */

#define ANIM_SPEED_LIST_ITEM    (g_anim_speed - 8) /* 列表项动画速度，适当慢于选择器 */
#define ANIM_SPEED_SELECTOR     (g_anim_speed)     /* 选择器动画速度 */
#define ANIM_SPEED_SELECTOR_H   (g_anim_speed + 1) /* 选择器高度动画稍快 */
#define ANIM_SPEED_INFO_BAR     (g_anim_speed + 2) /* 信息栏动画速度 */
#define ANIM_SPEED_INFO_BAR_W   (g_anim_speed + 3) /* 信息栏宽度动画稍快 */
#define ANIM_SPEED_POP_UP_W     (g_anim_speed + 4) /* 弹窗宽度动画稍快 */
#define ANIM_SPEED_POP_UP_Y     (g_anim_speed + 2) /* 弹窗 y 轴动画 */
#define ANIM_SPEED_CAMERA       (g_anim_speed + 4) /* 相机动画速度 */
#define ANIM_SPEED_EXIT         (g_anim_speed + 2) /* 退出动画速度 */

/* ═══ 字体 ═══ */

/**
 * @brief 设置当前绘图字体
 * @param _font 字体指针
 */
extern void xerintosh_set_font(const void* _font);

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

/* ═══ 回调类型 ═══ */

/**
 * @brief 统一回调函数类型
 * @param user_data 用户上下文指针（来自 xerintosh_list_item_t.user_data）
 */
typedef void (*xerintosh_cb_t)(void *user_data);

/* ═══ 列表项类型 ═══ */

#define MAX_LIST_CHILD_NUM 10   /* 每个父节点最多子项数 */
#define MAX_LIST_LAYER 10       /* 菜单树最大深度 */
#define LIST_ITEM_SPACING 18    /* 列表项纵向间距 */
#define LIST_ITEM_LEFT_MARGIN 4 /* 列表项左边距 */
#define LIST_ITEM_RIGHT_MARGIN 20  /* 列表项右边距（为右侧控件预留） */
#define LIST_INFO_BAR_HEIGHT 3  /* 信息栏高度补偿 */
#define LIST_FONT_TOP_MARGIN 6  /* 字体顶部边距 */

/**
 * @brief 菜单项类型枚举
 */
typedef enum
{
  list_item,
  switch_item,
  slider_item,
  user_item,
  button_item,
} xerintosh_list_item_type_t;

/**
 * @brief 列表项图标类型枚举
 */
typedef enum {
    default_icon,
    list_icon,
    switch_icon,
    plus_icon,
    user_icon,
    slider_icon,
    flag_icon,
    power_icon,
    custom_icon,    /* 自定义位图图标，需配合 bitmap_data 使用 */
} xerintosh_list_item_icon_t;

/* ═══ 列表项基类 ═══ */

/**
 * @brief 列表项基类结构体（所有派生类型的第一个字段）
 * @note  通过 C 风格 OOP 实现：基类必须作为派生结构的第一个成员
 */
typedef struct xerintosh_list_item_t
{
  xerintosh_list_item_type_t type;   /* 项类型 */
  xerintosh_list_item_icon_t icon;   /* 图标类型 */
  const char *content;               /* 显示文本 */

  uint8_t layer;                     /* 层级（根为 0） */
  float y_list_item, y_list_item_trg; /* 当前与目标 y 坐标 */
  uint8_t child_num;                 /* 子项数量 */
  struct xerintosh_list_item_t *child_list_item[MAX_LIST_CHILD_NUM];  /* 子项指针数组 */
  struct xerintosh_list_item_t *parent;  /* 父项指针 */
  void *user_data;                   /* 用户自定义数据 */

  /* 文字滚动状态 */
  uint32_t scroll_start_time;        /* 滚动开始时间戳 */
  bool is_scrolling;                 /* 是否正在滚动 */

  /* 自定义位图图标（仅当 icon == custom_icon 时有效） */
  const uint8_t *bitmap_data;        /* XBM 位图数据指针 */
  uint8_t bitmap_w;                  /* 位图宽度（像素） */
  uint8_t bitmap_h;                  /* 位图高度（像素） */
} xerintosh_list_item_t;

/* ═══ 派生类型 ═══ */

/**
 * @brief 开关项（绑定一个 bool* 指针）
 */
typedef struct xerintosh_switch_item_t
{
  xerintosh_list_item_t base_item;   /* 基类 */
  bool *value;                       /* 绑定的布尔值指针 */
  xerintosh_cb_t init_function;      /* 进入该项时调用的初始化函数 */
  xerintosh_cb_t exit_function;      /* 值改变后调用的退出函数 */
} xerintosh_switch_item_t;

/**
 * @brief 按钮项（单次触发回调）
 */
typedef struct xerintosh_button_item_t
{
  xerintosh_list_item_t base_item;   /* 基类 */
  xerintosh_cb_t exit_function;      /* 按下时触发的回调函数 */
} xerintosh_button_item_t;

/**
 * @brief 滑块项（绑定一个 int16_t* 指针，支持步进、最小值、最大值）
 */
typedef struct xerintosh_slider_item_t
{
  xerintosh_list_item_t base_item;   /* 基类 */
  int16_t *value;                    /* 绑定的数值指针 */
  int16_t value_backup;              /* 进入编辑模式时的备份值 */
  bool is_confirmed;                 /* 是否已确认修改 */
  uint8_t value_step;                /* 步进值 */
  int16_t value_max;                 /* 最大值 */
  int16_t value_min;                 /* 最小值 */
  xerintosh_cb_t init_function;      /* 进入该项时调用的初始化函数 */
  xerintosh_cb_t exit_function;      /* 值改变后调用的退出函数 */
} xerintosh_slider_item_t;

/**
 * @brief 用户自定义项（全屏 App 入口）
 */
typedef struct xerintosh_user_item_t
{
  xerintosh_list_item_t base_item;   /* 基类 */
  bool in_user_item;                 /* 是否已处于 user_item 运行态 */
  bool entering_user_item;           /* 是否正在进入 */
  bool exiting_user_item;            /* 是否正在退出 */
  xerintosh_cb_t init_function;      /* 进入时调用一次 */
  xerintosh_cb_t loop_function;      /* 每帧调用 */
  xerintosh_cb_t exit_function;      /* 退出时调用一次 */
  kern_pid_t kernel_pid;             /* 内核虚任务 PID（-1=未注册） */
} xerintosh_user_item_t;

/* ═══ 列表项操作 ═══ */

/**
 * @brief  获取根节点（单例，不会重复创建）
 * @return 根节点指针；内存分配失败时返回 NULL
 */
extern xerintosh_list_item_t *xerintosh_get_root_list(void);

/**
 * @brief  安全类型转换：转为 switch_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配时返回根节点
 */
extern xerintosh_switch_item_t *xerintosh_to_switch_item(xerintosh_list_item_t *_item);

/**
 * @brief  安全类型转换：转为 button_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配时返回根节点
 */
extern xerintosh_button_item_t *xerintosh_to_button_item(xerintosh_list_item_t *_item);

/**
 * @brief  安全类型转换：转为 slider_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配时返回根节点
 */
extern xerintosh_slider_item_t *xerintosh_to_slider_item(xerintosh_list_item_t *_item);

/**
 * @brief  安全类型转换：转为 user_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配时返回根节点
 */
extern xerintosh_user_item_t *xerintosh_to_user_item(xerintosh_list_item_t *_item);

/**
 * @brief  创建普通列表项
 * @param  _content 显示文本
 * @param  icon     图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
extern xerintosh_list_item_t *xerintosh_new_list_item(const char *_content, xerintosh_list_item_icon_t icon);

/**
 * @brief  创建开关项
 * @param  _content       显示文本
 * @param  _value         绑定的布尔值指针
 * @param  _init_function 进入该项时调用的初始化函数（可为 NULL）
 * @param  _exit_function 值改变后调用的退出函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
extern xerintosh_list_item_t *xerintosh_new_switch_item(const char *_content, bool *_value,
                                                 xerintosh_cb_t _init_function, xerintosh_cb_t _exit_function,
                                                 xerintosh_list_item_icon_t icon);

/**
 * @brief  创建按钮项
 * @param  _content       显示文本
 * @param  _exit_function 按下时触发的回调函数
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
extern xerintosh_list_item_t *xerintosh_new_button_item(const char *_content, xerintosh_cb_t _exit_function,
                                                 xerintosh_list_item_icon_t icon);

/**
 * @brief  创建滑块项
 * @param  _content       显示文本
 * @param  _value         绑定的数值指针
 * @param  _step          步进值
 * @param  _min           最小值
 * @param  _max           最大值
 * @param  _init_function 进入该项时调用的初始化函数（可为 NULL）
 * @param  _exit_function 值改变后调用的退出函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
extern xerintosh_list_item_t *xerintosh_new_slider_item(const char *_content, int16_t *_value, uint8_t _step,
                                                 int16_t _min, int16_t _max,
                                                 xerintosh_cb_t _init_function, xerintosh_cb_t _exit_function,
                                                 xerintosh_list_item_icon_t icon);

/**
 * @brief  创建用户自定义项（全屏 App 入口）
 * @param  _content       显示文本
 * @param  _init_function 进入时调用一次的初始化函数（可为 NULL）
 * @param  _loop_function 每帧调用的循环函数（可为 NULL）
 * @param  _exit_function 退出时调用一次的清理函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 */
extern xerintosh_list_item_t *xerintosh_new_user_item(const char *_content, xerintosh_cb_t _init_function,
                                               xerintosh_cb_t _loop_function, xerintosh_cb_t _exit_function,
                                               xerintosh_list_item_icon_t icon);

/**
 * @brief  将子项挂载到父项下
 * @param  _parent 父项指针
 * @param  _child  子项指针
 * @return true  挂载成功
 * @return false 挂载失败（子项已满、层级超限、参数为 NULL）
 */
extern bool xerintosh_push_item_to_list(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_child);

/**
 * @brief  从父项中移除指定子项
 * @param  _parent 父项指针
 * @param  _child  要移除的子项指针
 * @return true   移除成功
 * @return false  移除失败（未找到、参数为 NULL）
 * @note   会自动释放子项内存及其 content、user_data
 */
extern bool xerintosh_remove_item_from_list(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_child);

/**
 * @brief  清空父项下的所有子项
 * @param  _parent 父项指针
 */
extern void xerintosh_clear_children_of_list(xerintosh_list_item_t *_parent);

/**
 * @brief  安全移出选择器：若选择器位于即将被移除的子树内，将其移回父项
 * @param  subtree_root     即将被移除的子树根节点
 * @param  fallback_parent  选择器移出后的目标父项
 */
extern void ui_selector_safety_move_out(xerintosh_list_item_t *subtree_root,
                                        xerintosh_list_item_t *fallback_parent);

/**
 * @brief  rebuild 锚定：若选择器位于子树内，将其提升到子树根节点
 * @param  subtree_root 即将被重建的子树根节点
 * @param  parent       subtree_root 的父项（用于计算 selected_index）
 */
extern void ui_selector_rebuild_anchor(xerintosh_list_item_t *subtree_root,
                                       xerintosh_list_item_t *parent);

/**
 * @brief  若选择器当前在 parent 上，移至其第一个子项
 * @param  parent 父项指针
 */
extern void ui_selector_move_to_first_child(xerintosh_list_item_t *parent);

/* ═══ 选择器 ═══ */

/**
 * @brief 选择器结构体（高亮框状态）
 */
typedef struct xerintosh_selector_t
{
  float y_selector, y_selector_trg, w_selector, w_selector_trg, h_selector, h_selector_trg;  /* 位置与尺寸 */
  uint8_t selected_index;        /* 当前选中索引 */
  xerintosh_list_item_t *selected_item;  /* 当前选中项指针 */
} xerintosh_selector_t;

/**
 * @brief  将指定项绑定到选择器
 * @param  _item 要绑定的列表项
 * @return true  绑定成功
 * @return false 绑定失败（参数为 NULL 或父项为 NULL）
 */
extern bool xerintosh_bind_item_to_selector(xerintosh_list_item_t *_item);

/**
 * @brief 选择器移至下一项（循环）
 * @note  若当前为 slider_item 编辑模式，则增加数值
 */
extern void xerintosh_selector_go_next_item(void);

/**
 * @brief 选择器移至上一项（循环）
 * @note  若当前为 slider_item 编辑模式，则减少数值
 */
extern void xerintosh_selector_go_prev_item(void);

/**
 * @brief 确认/进入当前选中的项
 * @note  根据项类型执行不同操作：list_item 进入子菜单、switch_item 翻转值、
 *        slider_item 切换确认态、button_item 触发回调、user_item 进入全屏 App
 */
extern void xerintosh_selector_jump_to_selected_item(void);

/**
 * @brief 返回/退出当前项
 * @note  根据项类型执行不同操作：slider_item 取消编辑、user_item 退出、
 *        list_item 返回父菜单；主菜单（layer==0）不允许退出
 */
extern void xerintosh_selector_exit_current_item(void);

/**
 * @brief  user_item 通用退出检测（供 App loop 调用）
 * @param  event_b 按钮 B 的事件
 * @return true 若已触发退出，false 若事件不匹配
 * @note   长按 B 时自动退出当前 user_item，App 无需自行实现退出逻辑
 */
extern bool ui_user_item_try_exit(hal_event_t event_b);

/* ═══ 相机 ═══ */

/**
 * @brief 相机结构体（视图滚动偏移）
 * @note  负责将选择器始终保持在屏幕可视区域内
 */
typedef struct xerintosh_camera_t
{
  float x_camera, x_camera_trg, y_camera, y_camera_trg;  /* 当前与目标偏移 */
  xerintosh_selector_t *selector;  /* 绑定的选择器 */
} xerintosh_camera_t;

/**
 * @brief  将选择器绑定到相机
 * @param  _selector 选择器指针
 */
extern void xerintosh_bind_selector_to_camera(xerintosh_selector_t *_selector);

/* ═══ 向后兼容：子系统实例宏 ═══ */
/* ui_context 中使用指针存储这些结构体，这里通过解引用宏保持对现有代码的兼容 */

#define g_xerintosh_selector        (*(xerintosh_get_context()->selector))
#define g_xerintosh_camera          (*(xerintosh_get_context()->camera))
#define g_xerintosh_info_bar        (*(xerintosh_get_context()->info_bar))
#define g_xerintosh_pop_up          (*(xerintosh_get_context()->pop_up))

#ifdef __cplusplus
}
#endif

#endif /* UI_ITEM_H */
