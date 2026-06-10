/**
 * @file   flasher_gpio.cpp
 * @brief  烧录器 GPIO 引脚映射配置实现
 * @details 管理烧录器物理 GPIO 引脚与逻辑信号角色之间的映射关系。
 *          支持从 NVS 持久化加载/保存配置，以及运行时角色冲突自动处理。
 *          BOOT 引脚同时充当 DTR（复位）功能，DTR 信号自动回退至 BOOT。
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
            /* 跳过已废弃的 DTR(3) 和 RTS(4) 角色 */
            if (saved == 3 || saved == 4)
                saved = FLASHER_SIG_NONE;
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

#ifndef NATIVE_TEST
#include <Arduino.h>

static HardwareSerial *s_flasher_uart = nullptr;

void flasher_init_pins(uint32_t baud_rate)
{
    uint8_t tx_pin = flasher_get_pin_for_signal(FLASHER_SIG_TX);
    uint8_t rx_pin = flasher_get_pin_for_signal(FLASHER_SIG_RX);
    if (tx_pin == 255 || rx_pin == 255) return;

    if (s_flasher_uart == nullptr) {
        s_flasher_uart = &Serial1;
    }
    s_flasher_uart->begin(baud_rate, SERIAL_8N1, rx_pin, tx_pin);

    uint8_t boot_pin = flasher_get_pin_for_signal(FLASHER_SIG_BOOT);
    if (boot_pin != 255) { pinMode(boot_pin, OUTPUT); digitalWrite(boot_pin, HIGH); }
}

/**
 * @brief 设置 DTR 信号电平
 * @note  DTR 自动回退到 BOOT 引脚（复用）。
 *        DTR active=LOW 触发目标板复位进入 bootloader。
 */
void flasher_set_dtr(bool active) {
    uint8_t pin = flasher_get_pin_for_signal(FLASHER_SIG_BOOT);
    if (pin != 255) digitalWrite(pin, active ? LOW : HIGH);
}

void flasher_set_boot(bool low) {
    uint8_t pin = flasher_get_pin_for_signal(FLASHER_SIG_BOOT);
    if (pin != 255) digitalWrite(pin, low ? LOW : HIGH);
}

void flasher_enter_download_mode(void) {
    flasher_set_boot(true);  /* LOW */
    delay(100);
}

void flasher_reset_target(void) {
    flasher_set_boot(false); /* HIGH */
    delay(100);
}

int flasher_uart_write(const uint8_t *data, int len) {
    if (!s_flasher_uart || !data || len <= 0) return 0;
    return s_flasher_uart->write(data, len);
}

int flasher_uart_read(uint8_t *buf, int max_len) {
    if (!s_flasher_uart || !buf || max_len <= 0) return 0;
    int n = 0;
    while (n < max_len && s_flasher_uart->available()) {
        buf[n++] = s_flasher_uart->read();
    }
    return n;
}

#else /* NATIVE_TEST */
void flasher_init_pins(uint32_t baud_rate) { (void)baud_rate; }
void flasher_set_dtr(bool a) { (void)a; }
void flasher_set_boot(bool l) { (void)l; }
void flasher_enter_download_mode(void) {}
void flasher_reset_target(void) {}
int flasher_uart_write(const uint8_t *d, int l) { (void)d; (void)l; return l; }
int flasher_uart_read(uint8_t *b, int m) { (void)b; (void)m; return 0; }
#endif
