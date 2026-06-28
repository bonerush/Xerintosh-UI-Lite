/**
 * @file   hal_axp192.cpp
 * @brief  AXP192 PMIC 底层 I2C 实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：返回固定成功/失败的桩实现
 *          - 硬件环境时：使用 ESP-IDF I2C master 驱动封装
 *
 * @copyright Copyright (c) 2026
 */

#include "hal_axp192.h"

#ifndef NATIVE_TEST

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "hal_axp192";

static i2c_master_bus_handle_t g_axp192_bus = NULL;
static i2c_master_dev_handle_t g_axp192_dev = NULL;

esp_err_t hal_axp192_init(void) {
    if (g_axp192_dev != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AXP192_I2C_SDA_GPIO,
        .scl_io_num = AXP192_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = 0,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &g_axp192_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %d", err);
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP192_I2C_ADDR,
        .scl_speed_hz = AXP192_I2C_CLK_HZ,
        .scl_wait_us = 0,
        .flags = {},
    };

    err = i2c_master_bus_add_device(g_axp192_bus, &dev_cfg, &g_axp192_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AXP192 device: %d", err);
        i2c_del_master_bus(g_axp192_bus);
        g_axp192_bus = NULL;
        return err;
    }

    return ESP_OK;
}

esp_err_t hal_axp192_read_reg(uint8_t reg, uint8_t *out) {
    if (g_axp192_dev == NULL) {
        esp_err_t err = hal_axp192_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    return i2c_master_transmit_receive(g_axp192_dev, &reg, 1, out, 1, 100);
}

esp_err_t hal_axp192_write_reg(uint8_t reg, uint8_t val) {
    if (g_axp192_dev == NULL) {
        esp_err_t err = hal_axp192_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(g_axp192_dev, buf, sizeof(buf), 100);
}

#else /* NATIVE_TEST */

esp_err_t hal_axp192_init(void) { return ESP_OK; }

esp_err_t hal_axp192_read_reg(uint8_t reg, uint8_t *out) {
    (void)reg;
    if (out) *out = 0;
    return ESP_OK;
}

esp_err_t hal_axp192_write_reg(uint8_t reg, uint8_t val) {
    (void)reg;
    (void)val;
    return ESP_OK;
}

#endif /* NATIVE_TEST */
