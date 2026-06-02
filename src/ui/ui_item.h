/**
 * @file   ui_item.h
 * @brief  Xerintosh UI 菜单项系统聚合头文件
 * @details 本文件为向后兼容的聚合头文件，包含 ui_types.h、ui_widget.h、
 *          ui_item_core.h、ui_selector.h、ui_camera.h 五个子模块。
 *          新代码建议按需只包含所需子头文件，以降低编译依赖。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_ITEM_H
#define UI_ITEM_H

/* 向后兼容：保留 HAL 头文件包含，避免现有 .c 文件因拆分而需要额外添加包含 */
#include "hal/hal_display.h"
#include "hal/hal_input.h"

/* 子模块头文件 */
#include "ui_types.h"
#include "ui_widget.h"
#include "ui_item_core.h"
#include "ui_selector.h"
#include "ui_camera.h"

#endif /* UI_ITEM_H */
