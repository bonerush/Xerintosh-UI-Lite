#include "ui_draw_driver.h"
#include "hal/hal_input.h"

void xerintosh_ui_driver_init(void)
{
    hal_display_init();
    hal_system_init();
    hal_input_init();
}
