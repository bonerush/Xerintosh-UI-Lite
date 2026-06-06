/**
 * @file   flasher_gpio.cpp
 * @brief  烧录器 GPIO 引脚映射配置实现
 * @details 管理烧录器物理 GPIO 引脚与逻辑信号角色之间的映射关系。
 *          支持从 NVS 持久化加载/保存配置，以及运行时角色冲突自动处理。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher_gpio.h"
#include "app/storage/storage.h"

/* 默认映射：G0=BOOT, G26=TX, G36=RX */
flasher_pin_mapping_t g_flasher_pins[FLASHER_AVAILABLE_PINS] = {
    {0,  FLASHER_SIG_BOOT, true},
    {26, FLASHER_SIG_TX,   true},
    {36, FLASHER_SIG_RX,   false}
};

bool flasher_set_pin_role(uint8_t pin, flasher_signal_t role)
{
    int idx = -1;
    for (int i = 0; i < FLASHER_AVAILABLE_PINS; i++) {
        if (g_flasher_pins[i].pin_num == pin) { idx = i; break; }
    }
    if (idx < 0) return false;
    /* G36 cannot be output */
    if (!g_flasher_pins[idx].can_output && role != FLASHER_SIG_NONE && role != FLASHER_SIG_RX) {
        return false;
    }
    /* Remove duplicate: if another pin already has this role, clear it */
    for (int i = 0; i < FLASHER_AVAILABLE_PINS; i++) {
        if (i != idx && g_flasher_pins[i].role == role) {
            g_flasher_pins[i].role = FLASHER_SIG_NONE;
        }
    }
    g_flasher_pins[idx].role = role;
    return true;
}

uint8_t flasher_get_pin_for_signal(flasher_signal_t sig)
{
    for (int i = 0; i < FLASHER_AVAILABLE_PINS; i++) {
        if (g_flasher_pins[i].role == sig) return g_flasher_pins[i].pin_num;
    }
    return 255;
}

void flasher_load_pin_config(void)
{
    for (int i = 0; i < FLASHER_AVAILABLE_PINS; i++) {
        uint8_t saved = storage_get_flasher_pin_role(g_flasher_pins[i].pin_num);
        if (saved < FLASHER_SIG_COUNT) {
            flasher_set_pin_role(g_flasher_pins[i].pin_num, (flasher_signal_t)saved);
        }
    }
}

void flasher_save_pin_config(void)
{
    for (int i = 0; i < FLASHER_AVAILABLE_PINS; i++) {
        storage_set_flasher_pin_role(g_flasher_pins[i].pin_num, (uint8_t)g_flasher_pins[i].role);
    }
}
