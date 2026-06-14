/**
 * @file   ui_item.h
 * @brief  Xerintosh UI 菜单项系统 C/C++ 兼容聚合头文件
 * @details 本文件为向后兼容的聚合头文件，包含 ui_types.h、ui_widget.h、
 *          ui_item_core.h、ui_selector.h、ui_camera.h 五个子模块。
 *          新代码建议按需只包含所需子头文件，以降低编译依赖。
 *          本文件使用 extern "C" 包裹，可被 C++ 翻译单元安全包含。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_ITEM_H
#define UI_ITEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* 向后兼容：保留 HAL 头文件包含，避免现有 .c 文件因拆分而需要额外添加包含 */
#include "hal/hal_display.h"
#include "hal/hal_input.h"

/* 全局上下文（替代分散的全局变量） */
#include "ui/ui_context.h"

/* 子模块头文件 */
#include "ui_types.h"
#include "ui_widget.h"
#include "ui_item_core.h"
#include "ui_selector.h"
#include "ui_camera.h"

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
