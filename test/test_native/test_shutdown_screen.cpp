#include <gtest/gtest.h>

extern "C" {
#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "app/shutdown/shutdown_screen.h"
}

/* ═══ 关机界面测试 ═══ */

/**
 * @brief shutdown_screen_show() 不应崩溃
 */
TEST(ShutdownScreenTest, ShowDoesNotCrash)
{
    hal_system_init();
    hal_display_init();

    /* 调用关机界面：在 native 环境下延时应正常完成 */
    shutdown_screen_show();

    /* 只要不崩溃就算通过 */
    SUCCEED();
}

/**
 * @brief native 环境下 shutdown_screen_power_off() 是空操作
 */
TEST(ShutdownScreenTest, PowerOffIsNoopInNative)
{
    /* 在 native 环境下，此函数应为空操作，调用后程序继续执行 */
    shutdown_screen_power_off();
    SUCCEED();
}
