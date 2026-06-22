/**
 * @file   hal_axp192.h
 * @brief  AXP192 PMIC 底层 I2C 封装
 * @details ESP-IDF 原生 I2C 驱动封装，替代 M5Unified 中的 AXP192_Class。
 *          目前仅暴露电源键检测与硬件关机所需的寄存器读写接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_AXP192_H
#define HAL_AXP192_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define AXP192_I2C_ADDR        0x34  /* AXP192 7-bit I2C 地址 */
#define AXP192_I2C_SDA_GPIO    GPIO_NUM_21
#define AXP192_I2C_SCL_GPIO    GPIO_NUM_22
#define AXP192_I2C_CLK_HZ      100000 /* 100 kHz，AXP192 支持标准模式 */

#define AXP192_REG_POWER_OFF   0x32  /* 关机控制寄存器 */
#define AXP192_REG_IRQ_STATUS3 0x46  /* IRQ 状态 3：电源键短按/长按 */

#define AXP192_PEK_SHORT_PRESS 0x02  /* IRQ3 bit1: 短按 */
#define AXP192_PEK_LONG_PRESS  0x01  /* IRQ3 bit0: 长按 */

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 AXP192 I2C 总线
 * @return ESP_OK 成功，其他为失败
 * @note   幂等：重复调用不会重复初始化
 */
esp_err_t hal_axp192_init(void);

/* ═══ 寄存器读写 ═══ */

/**
 * @brief  读取单个寄存器
 * @param  reg  寄存器地址
 * @param  out  输出缓冲区
 * @return ESP_OK 成功
 */
esp_err_t hal_axp192_read_reg(uint8_t reg, uint8_t *out);

/**
 * @brief  写入单个寄存器
 * @param  reg 寄存器地址
 * @param  val 要写入的值
 * @return ESP_OK 成功
 */
esp_err_t hal_axp192_write_reg(uint8_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AXP192_H */
