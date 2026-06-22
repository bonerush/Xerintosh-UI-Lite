/**
 * @file   hal_uart.h
 * @brief  UART 硬件抽象层
 * @details ESP-IDF 原生 UART 驱动封装，提供一次性初始化和非阻塞读写接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 UART0（控制台/USB 串口）
 * @note  幂等：重复调用安全
 */
void hal_uart0_init(void);

/* ═══ 数据读写 ═══ */

/**
 * @brief  从 UART0 非阻塞读取数据
 * @param  buf 接收缓冲区
 * @param  len 最大读取长度
 * @return 实际读取字节数
 */
int hal_uart0_read(uint8_t *buf, int len);

/**
 * @brief  向 UART0 写入数据
 * @param  data 发送数据
 * @param  len  发送长度
 * @return 实际写入字节数
 */
int hal_uart0_write(const uint8_t *data, int len);

/**
 * @brief  查询 UART0 可读数据量
 * @return 可读字节数
 */
int hal_uart0_available(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
