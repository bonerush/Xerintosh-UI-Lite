/**
 * @file   hal_bt.h
 * @brief  Bluetooth 硬件抽象层 (Classic BT SPP)
 * @details ESP-IDF 原生 BT API 封装，提供 BT controller + Bluedroid + SPP
 *          的完整生命周期管理和非阻塞数据读写接口。
 *
 *          有线串口 (UART0) 经由 hal_uart.h，蓝牙串口经由 hal_bt.h。
 *          两者对上层暴露统一的"打开/读写/关闭"语义。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_BT_H
#define HAL_BT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief  初始化 Bluetooth 子系统（Controller + Bluedroid + SPP）
 * @return true  初始化成功
 * @return false 初始化失败
 * @note   重复调用安全（幂等）
 */
bool hal_bt_init(void);

/**
 * @brief  反初始化 Bluetooth 子系统
 * @note   释放 Bluedroid 栈、BT Controller、SPP 所有资源
 */
void hal_bt_deinit(void);

/* ═══ SPP 数据读写 ═══ */

/**
 * @brief  从 SPP RX 缓冲区非阻塞读取
 * @param  buf 接收缓冲区
 * @param  len 最大读取长度
 * @return 实际读取字节数
 */
int hal_bt_spp_read(uint8_t *buf, int len);

/**
 * @brief  通过 SPP 发送数据
 * @param  data 发送数据
 * @param  len  发送长度
 * @return 实际发送字节数，未连接返回 0
 */
int hal_bt_spp_write(const uint8_t *data, int len);

/**
 * @brief  查询 SPP RX 缓冲区可读数据量
 * @return 可读字节数
 */
int hal_bt_spp_available(void);

/**
 * @brief  查询 SPP 是否已连接
 * @return true  有活跃的 SPP 连接
 */
bool hal_bt_spp_is_connected(void);

/* ═══ 后台轮询 ═══ */

/**
 * @brief  处理 SPP 回调事件，将 RX 数据从事件缓冲区移动到环形缓冲区
 * @note   由调用者定期调用（如每帧一次）。
 *         硬件环境下，SPP 数据在 Bluedroid 任务上下文中通过回调写入内部队列，
 *         此函数将队列中的数据转移到应用程序可读的环形缓冲区中。
 */
void hal_bt_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BT_H */
