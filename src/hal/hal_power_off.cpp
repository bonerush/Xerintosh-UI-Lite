/**
 * @file   hal_power_off.cpp
 * @brief  硬件关机 C++ 包装
 * @details 封装 M5.Power.powerOff() 为 C 可调用函数，
 *          通过 AXP192 寄存器实现真正硬件断电。
 */

#ifndef NATIVE_TEST

#include <M5Unified.h>

extern "C" void hal_power_off_hw(void)
{
    M5.Display.sleep();
    M5.Power.powerOff();
}

#else

extern "C" void hal_power_off_hw(void)
{
    /* native 测试环境：空操作 */
}

#endif
