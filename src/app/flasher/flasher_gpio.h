/**
 * @file   flasher_gpio.h
 * @brief  烧录器 GPIO 引脚映射配置头文件
 * @details 定义烧录器可用引脚、信号角色枚举及引脚映射管理接口。
 *          负责将逻辑信号（TX/RX/DTR/RTS/BOOT）映射到物理 GPIO 引脚。
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
    FLASHER_SIG_DTR  = 3,
    FLASHER_SIG_RTS  = 4,
    FLASHER_SIG_BOOT = 5,
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
 * @brief 从 NVS 加载引脚映射配置到全局数组
 */
void flasher_load_pin_config(void);

/**
 * @brief 将当前引脚映射配置保存到 NVS
 */
void flasher_save_pin_config(void);

/**
 * @brief  根据配置初始化 GPIO 引脚和 UART
 * @note   配置 TX/RX 引脚到 Serial1，设置 DTR/RTS/BOOT 为输出
 */
void flasher_init_pins(void);

/**
 * @brief  设置 DTR 信号电平
 * @param  active true=LOW（有效），false=HIGH（无效）
 */
void flasher_set_dtr(bool active);

/**
 * @brief  设置 RTS 信号电平
 * @param  active true=LOW（有效），false=HIGH（无效）
 */
void flasher_set_rts(bool active);

/**
 * @brief  设置 BOOT 引脚电平
 * @param  low true=LOW，false=HIGH
 */
void flasher_set_boot(bool low);

/**
 * @brief  将目标 ESP32 进入下载模式
 * @note   时序：BOOT=LOW, RTS=LOW, delay(100), RTS=HIGH, delay(100)
 *         BOOT 保持 LOW 直到烧录完成
 */
void flasher_enter_download_mode(void);

/**
 * @brief  复位目标 ESP32（正常启动）
 * @note   时序：BOOT=HIGH, RTS=LOW, delay(100), RTS=HIGH
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
