/**
 * @file   flasher_proto.h
 * @brief  烧录器协议自动识别引擎 — 公共头文件
 * @details 从 USB→UART 数据流中自动识别 STK500 (avrdude)、ESP32 SLIP (esptool)
 *          和 STM32 USART Bootloader (stm32flash) 协议，并计算烧录进度百分比。
 *          所有解析器内部状态封装在 .c 中，本头文件仅暴露最小接口。
 *
 *          使用流程：
 *              1. DTR 脉冲后调用 flasher_proto_reset() 清空状态。
 *              2. USB→UART 方向的每个字节调用 flasher_proto_feed(b)。
 *              3. 周期性调用 flasher_proto_get_progress() 获取进度 0-100。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef FLASHER_PROTO_H
#define FLASHER_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLASHER_PROTO_NONE = 0,     /**< 尚未检测到有效协议数据 */
    FLASHER_PROTO_STK500,       /**< 已确认 STK500 协议 (avrdude/Arduino) */
    FLASHER_PROTO_ESP32,        /**< 已确认 ESP32 SLIP 协议 (esptool) */
    FLASHER_PROTO_STM32         /**< 已确认 STM32 USART Bootloader (stm32flash) */
} flasher_proto_t;

/**
 * @brief  向协议识别引擎喂入一个 USB→UART 方向字节
 * @param  byte  原始字节
 * @return 当前已确认的协议类型
 * @note   未确认时多解析器同时运行；任一解析器先锁定后锁定协议类型。
 *         调用方可忽略返回值，改用 flasher_proto_get_progress() 轮询进度。
 */
flasher_proto_t flasher_proto_feed(uint8_t byte);

/**
 * @brief  重置所有协议解析器状态（DTR 复位 / 手动复位时调用）
 * @note   清空协议类型、STK500 状态机、SLIP 缓冲区、STM32 状态机、进度计数器
 */
void flasher_proto_reset(void);

/**
 * @brief  根据当前已确认协议获取烧录进度
 * @return 0-100 的百分比；未确认协议返回 0
 * @note   优先级：ESP32 > STM32 > STK500
 */
int flasher_proto_get_progress(void);

#ifdef __cplusplus
}
#endif

#endif
