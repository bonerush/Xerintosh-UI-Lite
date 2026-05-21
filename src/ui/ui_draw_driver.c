/**
 * @file   ui_draw_driver.c
 * @brief  UI 驱动初始化实现
 * @details 封装 HAL 三件套初始化调用，供 UI 框架入口使用。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_draw_driver.h"
#include "hal/hal_input.h"

/**
 * @brief 初始化 UI 驱动
 * @note  依次调用 hal_display_init、hal_system_init、hal_input_init
 */
void xerintosh_ui_driver_init(void)
{
    hal_display_init();
    hal_system_init();
    hal_input_init();
}
