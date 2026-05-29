/**
 * @file   ui_draw_driver.h
 * @brief  UI 驱动初始化头文件
 * @details 声明全局绘制颜色变量与 UI 驱动初始化接口。
 *          本文件不再包含 OLED → HAL 宏桥接；所有绘制代码应直接使用 hal/hal_display.h 中的 API。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_DRAW_DRIVER_H
#define UI_DRAW_DRIVER_H

#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 UI 驱动（依次初始化显示、系统时钟、按键输入）
 */
extern void xerintosh_ui_driver_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DRAW_DRIVER_H */
