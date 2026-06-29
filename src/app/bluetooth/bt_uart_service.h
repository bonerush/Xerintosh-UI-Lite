/**
 * @file   bt_uart_service.h
 * @brief  Bluetooth UART 服务头文件（Classic BT SPP）
 * @details 基于 ESP-IDF 原生 BT API (via hal_bt) 实现经典蓝牙串口通信。
 *          设备作为 SPP 外设，手机/电脑 SPP 客户端连接后进行双向串口通信。
 *
 *          双实现架构：
 *          - NATIVE_TEST 时：提供可测试的桩实现
 *          - 硬件环境时：包装 hal_bt 层，提供回调机制
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BT_UART_SERVICE_H
#define BT_UART_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量定义 ═══ */

#define BT_UART_TX_BUF_SIZE  512   /* 发送环形缓冲区大小（字节） */
#define BT_UART_RX_BUF_SIZE  512   /* 接收环形缓冲区大小（字节） */

/* ═══ 类型定义 ═══ */

/**
 * @brief 接收数据回调函数类型
 * @param  data  接收到的数据指针
 * @param  len   数据长度（字节）
 * @note   回调在轮询上下文中执行（Xeros 主任务），应尽快返回
 */
typedef void (*bt_uart_rx_callback_t)(const uint8_t *data, uint16_t len);

/**
 * @brief 连接状态变化回调函数类型
 * @param  connected  true=已连接, false=已断开
 */
typedef void (*bt_uart_connect_callback_t)(bool connected);

/* ═══ 生命周期 ═══ */

/**
 * @brief  初始化 Bluetooth UART 服务
 * @return true  初始化成功
 * @return false 初始化失败
 */
bool bt_uart_service_init(void);

/**
 * @brief 反初始化 Bluetooth UART 服务
 */
void bt_uart_service_deinit(void);

/* ═══ 数据发送 ═══ */

/**
 * @brief  发送数据（设备 → 手机）
 * @param  data  数据指针
 * @param  len   数据长度（字节）
 * @return 实际发送的字节数，失败返回 0
 */
uint16_t bt_uart_send(const uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串（便捷函数）
 * @param  str  以 null 结尾的字符串
 * @return 实际发送的字节数（不含 null 终止符），失败返回 0
 */
uint16_t bt_uart_send_string(const char *str);

/* ═══ 回调注册 ═══ */

/**
 * @brief 设置接收数据回调
 * @param  callback  回调函数指针，NULL 表示取消注册
 */
void bt_uart_set_rx_callback(bt_uart_rx_callback_t callback);

/**
 * @brief 设置连接状态变化回调
 * @param  callback  回调函数指针，NULL 表示取消注册
 */
void bt_uart_set_connect_callback(bt_uart_connect_callback_t callback);

/* ═══ 状态查询 ═══ */

/**
 * @brief  查询是否有客户端连接
 * @return true  已连接
 * @return false 未连接
 */
bool bt_uart_is_connected(void);

/**
 * @brief  获取发送缓冲区已用字节数
 * @return 已用字节数
 */
uint16_t bt_uart_get_tx_buffer_usage(void);

/**
 * @brief  获取接收缓冲区已用字节数
 * @return 已用字节数
 */
uint16_t bt_uart_get_rx_buffer_usage(void);

/* ═══ 后台轮询 ═══ */

/**
 * @brief  轮询蓝牙串口：读取数据并检测连接状态变化
 * @note   由调用者定期调用（如每帧一次，在 main_loop_task 中）。
 *         RX 数据处理通过已注册的回调函数分发。
 */
void bt_uart_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_UART_SERVICE_H */
