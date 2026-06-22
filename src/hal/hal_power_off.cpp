/**
 * @file   hal_power_off.cpp
 * @brief  硬件关机 C++ 包装
 * @details 通过 AXP192 寄存器实现真正硬件断电。
 *          显示休眠功能暂时留空，待 display HAL 迁移完成后接入。
 */

#ifndef NATIVE_TEST

#include "hal_axp192.h"

extern "C" void hal_power_off_hw(void)
{
    /* TODO: 待 display HAL 迁移完成后调用显示休眠 */
    /* hal_display_sleep(); */

    /* AXP192 reg 0x32 bit 7: 关机 */
    uint8_t val = 0;
    if (hal_axp192_read_reg(AXP192_REG_POWER_OFF, &val) == ESP_OK) {
        val |= 0x80;
        hal_axp192_write_reg(AXP192_REG_POWER_OFF, val);
    }
}

#else

extern "C" void hal_power_off_hw(void)
{
    /* native 测试环境：空操作 */
}

#endif
