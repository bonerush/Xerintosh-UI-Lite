/**
 * @file   ui_item_core.h
 * @brief  Xerintosh UI 列表项核心数据模型
 * @details 定义五种菜单项类型（list/switch/slider/button/user）的
 *          数据结构，以及创建、挂载、移除、类型转换等核心 API。
 *
 * ## 使用模板请参阅
 * - `doc/tutorials/api-templates.md` — 完整 API 调用模板与常见陷阱
 *
 * ## 核心规则
 * 1. 传给框架的指针必须永久有效 — `switch_item.value`、`slider_item.value`
 *    必须指向 `static` 或全局变量，严禁指向局部变量。
 * 2. `user_data` 的动态内存由调用方自行管理 — 若指向 `malloc` 内存，
 *    必须在 `user_item` 上设置 `destroy_callback` 释放。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_ITEM_CORE_H
#define UI_ITEM_CORE_H

#include "ui_types.h"
#include "kernel/kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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
  xerintosh_cb_t init_function;      /* 进入子菜单时调用的初始化函数（可为 NULL） */
} xerintosh_list_item_t;

/* ═══ 派生类型 ═══ */

/**
 * @brief 开关项（绑定一个 bool* 指针）
 * @warning value 指针必须指向 static 或全局变量，框架不管理其生命周期。
 *          指向局部变量会导致悬空指针（use-after-free）。
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
 * @warning 按钮回调中不能直接调用 xerintosh_push_pop_up() 或显示层绘制函数，
 *          在调度上下文中可能超时。
 *          正确做法：设标志位，由主循环的 app_input_process() 统一处理。
 */
typedef struct xerintosh_button_item_t
{
  xerintosh_list_item_t base_item;   /* 基类 */
  xerintosh_cb_t exit_function;      /* 按下时触发的回调函数 */
} xerintosh_button_item_t;

/**
 * @brief 滑块项（绑定一个 int16_t* 指针，支持步进、最小值、最大值）
 * @warning value 指针必须指向 static 或全局变量，框架不管理其生命周期。
 * @note  操作逻辑：第1次确认→进入编辑模式（上下键改为增减数值），
 *        第2次确认→保存并退出，长按返回→取消并恢复备份值。
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
 * @note  生命周期：init()→loop()每帧→exit()。退出检查用 ui_user_item_try_exit()。
 *        若 user_data 是动态分配的，必须设置 destroy_callback 释放。
 *        init() 和 exit() 中必须调用 hal_input_reset_events() 清除残留按键。
 *        loop() 中不需要调用 hal_display_clear() — 框架统一处理。
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
  xerintosh_cb_t destroy_callback;   /* 销毁时调用，供 App 清理 user_data */
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
 * @return 转换后的指针；类型不匹配或参数为 NULL 时返回 NULL
 */
extern xerintosh_switch_item_t *xerintosh_to_switch_item(xerintosh_list_item_t *_item);

/**
 * @brief  安全类型转换：转为 button_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配或参数为 NULL 时返回 NULL
 */
extern xerintosh_button_item_t *xerintosh_to_button_item(xerintosh_list_item_t *_item);

/**
 * @brief  安全类型转换：转为 slider_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配或参数为 NULL 时返回 NULL
 */
extern xerintosh_slider_item_t *xerintosh_to_slider_item(xerintosh_list_item_t *_item);

/**
 * @brief  安全类型转换：转为 user_item
 * @param  _item 列表项指针
 * @return 转换后的指针；类型不匹配或参数为 NULL 时返回 NULL
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
 * @param  _value         绑定的布尔值指针（必须指向 static/全局变量！）
 * @param  _init_function 进入该项时调用的初始化函数（可为 NULL）
 * @param  _exit_function 值改变后调用的退出函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 * @warning _value 指针的生命周期必须长于菜单生命周期，严禁传入局部变量地址。
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
 * @warning 回调中禁止调用 xerintosh_push_pop_up() / 显示层绘制函数，
 *          应设标志位由主循环 app_input_process() 统一处理。
 */
extern xerintosh_list_item_t *xerintosh_new_button_item(const char *_content, xerintosh_cb_t _exit_function,
                                                 xerintosh_list_item_icon_t icon);

/**
 * @brief  创建滑块项
 * @param  _content       显示文本
 * @param  _value         绑定的数值指针（必须指向 static/全局变量！）
 * @param  _step          步进值
 * @param  _min           最小值
 * @param  _max           最大值
 * @param  _init_function 进入该项时调用的初始化函数（可为 NULL）
 * @param  _exit_function 值改变后调用的退出函数（可为 NULL）
 * @param  icon           图标类型
 * @return 新创建的列表项指针；内存分配失败时返回 NULL
 * @warning _value 指针的生命周期必须长于菜单生命周期，严禁传入局部变量地址。
 * @note   编辑模式下上下键切换为增减数值而非导航菜单项。
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
 * @note   destroy_callback 创建时默认为 NULL，需调用方手动设置以清理 user_data。
 * @note   loop() 中应调用 ui_user_item_try_exit(event_b) 检查退出请求。
 * @note   init() / exit() 中必须调用 hal_input_reset_events() 清除残留按键。
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
 * @brief  递归释放菜单项及其所有子节点
 * @param  _item 要释放的菜单项指针
 * @note   会调用 item 的 destroy_callback（如果存在）
 * @warning 此函数不负责从父项的 child_list_item[] 数组中移除指针。
 *          应先调用 xerintosh_remove_item_from_list() 再销毁，
 *          或用 xerintosh_clear_children_of_list() 一次性清理。
 */
extern void xerintosh_destroy_item_tree(xerintosh_list_item_t *_item);

/**
 * @brief  通过派发表执行确认/进入操作
 * @param  item 列表项指针
 */
extern void xerintosh_dispatch_enter(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表处理“下一项”输入
 * @param  item 当前选中的列表项指针
 * @return true  输入已被类型特定逻辑消费（如 slider 编辑模式增加值）
 * @return false 输入未被消费，调用方应执行默认导航
 */
extern bool xerintosh_dispatch_input_next(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表处理“上一项”输入
 * @param  item 当前选中的列表项指针
 * @return true  输入已被类型特定逻辑消费（如 slider 编辑模式减少值）
 * @return false 输入未被消费，调用方应执行默认导航
 */
extern bool xerintosh_dispatch_input_prev(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表处理“返回/退出”输入
 * @param  item 当前选中的列表项指针
 * @return true  输入已被类型特定逻辑消费（如 slider 取消编辑、user_item 触发退出）
 * @return false 输入未被消费，调用方应执行默认返回导航
 */
extern bool xerintosh_dispatch_input_exit(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表测量当前选择器宽度
 * @param  item 当前选中的列表项指针
 * @return 选择器目标宽度（像素）
 */
extern int16_t xerintosh_dispatch_measure(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表绘制列表项
 * @param  item 要绘制的列表项指针
 * @param  x    图标左上角 x 坐标
 * @param  y    项中心 y 坐标
 */
extern void xerintosh_dispatch_draw(xerintosh_list_item_t *item, int16_t x, int16_t y);

/**
 * @brief  通过派发表绘制项的覆盖层（如已确认 slider 的数值反色框）
 * @param  item 要绘制覆盖层的列表项指针
 */
extern void xerintosh_dispatch_draw_overlay(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表执行类型特定的销毁清理
 * @param  item 要销毁的列表项指针
 */
extern void xerintosh_dispatch_destroy(xerintosh_list_item_t *item);

/**
 * @brief  通过派发表判断该项是否有右侧控件（影响文字可用宽度）
 * @param  item 列表项指针
 * @return true  有右侧控件（switch/slider）
 * @return false 无右侧控件
 */
extern bool xerintosh_dispatch_has_right_control(xerintosh_list_item_t *item);

#ifdef __cplusplus
}
#endif

#endif /* UI_ITEM_CORE_H */
