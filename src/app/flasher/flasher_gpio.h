/**
 * @file   flasher_gpio.h
 * @brief  烧录器 GPIO 引脚映射配置头文件
 * @details 定义烧录器可用引脚、信号角色枚举及引脚映射管理接口。
 *          负责将逻辑信号（TX/RX/BOOT）映射到物理 GPIO 引脚。
 *          BOOT 引脚同时充当 DTR（复位）功能。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef FLASHER_GPIO_H
#define FLASHER_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLASHER_AVAILABLE_PINS 3

typedef enum {
    FLASHER_SIG_NONE = 0,
    FLASHER_SIG_TX   = 1,
    FLASHER_SIG_RX   = 2,
    FLASHER_SIG_BOOT = 5,  /**< BOOT/DTR 复用引脚 */
    FLASHER_SIG_COUNT = 6
} flasher_signal_t;

typedef struct {
    uint8_t pin_num;
    flasher_signal_t role;
    bool can_output;
} flasher_pin_mapping_t;

/* 默认映射在 .cpp 中初始化：G0=BOOT, G26=TX, G36=RX */
extern flasher_pin_mapping_t g_flasher_pins[FLASHER_AVAILABLE_PINS];

/**
 * @brief  设置指定引脚的角色
 * @param  pin  引脚编号
 * @param  role 信号角色
 * @return true  设置成功
 * @return false 引脚不存在或该引脚不支持此角色（如输入-only 引脚被设为输出角色）
 */
bool flasher_set_pin_role(uint8_t pin, flasher_signal_t role);

/**
 * @brief  根据信号角色查找对应的引脚编号
 * @param  sig 信号角色
 * @return 引脚编号；未找到返回 255
 */
uint8_t flasher_get_pin_for_signal(flasher_signal_t sig);

/**
 * @brief  将信号角色转换为可显示标签
 * @param  role 信号角色
 * @return 角色标签字符串（静态字面量或常量）
 */
const char* flasher_role_label(flasher_signal_t role);

/**
 * @brief 从 NVS 加载引脚映射配置到全局数组
 */
void flasher_load_pin_config(void);

/**
 * @brief 将当前引脚映射配置保存到 NVS
 */
void flasher_save_pin_config(void);

/**
 * @brief  根据配置初始化 GPIO 引脚和 UART
 * @param  baud_rate UART 波特率（如 115200, 57600）
 * @note   配置 TX/RX 引脚到 Serial1，设置 BOOT 为输出高电平
 */
void flasher_init_pins(uint32_t baud_rate);

/**
 * @brief  设置 DTR 信号电平（自动回退到 BOOT 引脚）
 * @param  active true=LOW（有效），false=HIGH（无效）
 */
void flasher_set_dtr(bool active);

/**
 * @brief  设置 BOOT 引脚电平
 * @param  low true=LOW，false=HIGH
 */
void flasher_set_boot(bool low);

/**
 * @brief  将目标 ESP32 进入下载模式
 * @note   时序：BOOT=LOW, delay(100)
 *         有线桥接模式通过 avrdude DTR 脉冲自动复位，此函数保留备用。
 */
void flasher_enter_download_mode(void);

/**
 * @brief  复位目标设备（正常启动）
 * @note   时序：BOOT=HIGH, delay(100)
 */
void flasher_reset_target(void);

/**
 * @brief  通过烧录器 UART 发送数据
 * @param  data 数据缓冲区
 * @param  len  数据长度
 * @return 实际发送的字节数
 */
int flasher_uart_write(const uint8_t *data, int len);

/**
 * @brief  从烧录器 UART 读取数据
 * @param  buf 接收缓冲区
 * @param  max_len 最大读取长度
 * @return 实际读取的字节数
 */
int flasher_uart_read(uint8_t *buf, int max_len);

#ifdef __cplusplus
}
#endif

#endif
