/**
 * @file   ui_service.c
 * @brief  App 层 UI 公共服务实现
 * @details 统一封装 user_item 生命周期中的公共操作。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_service.h"

#include "hal/hal_input.h"
#include "ui/ui_item.h"

void ui_service_user_item_init(void)
{
    hal_input_reset_events();
}

bool ui_service_user_item_loop(hal_event_t event_b)
{
    return ui_user_item_try_exit(event_b);
}

void ui_service_user_item_exit(void)
{
    hal_input_reset_events();
}
