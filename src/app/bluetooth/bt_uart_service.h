/**
 * @file   bt_uart_service.h
 * @brief  Bluetooth UART 服务头文件（Classic Bluetooth SPP）
 * @details 基于 ESP32 Arduino BluetoothSerial 实现经典蓝牙串口通信。
 *          设备作为外设，手机/电脑作为中心设备连接。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BT_UART_SERVICE_H
#define BT_UART_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量定义 ═══ */

#define BT_UART_TX_BUF_SIZE  512   /* 发送环形缓冲区大小（字节） */
#define BT_UART_RX_BUF_SIZE  512   /* 接收环形缓冲区大小（字节） */
#define BT_UART_DEFAULT_MTU  20    /* 兼容旧 API 保留常量名，SPP 无 MTU 限制 */

/* ═══ 类型定义 ═══ */

/**
 * @brief Bluetooth UART 服务初始化错误码
 */
typedef enum {
    BT_UART_OK = 0,
    BT_UART_ERR_HEAP,        /* 内存不足 */
    BT_UART_ERR_BLUEDROID,   /* Bluedroid 初始化/状态错误 */
    BT_UART_ERR_RADIO,       /* BT controller 射频/状态错误 */
    BT_UART_ERR_UNKNOWN,     /* 其他未知错误 */
} bt_uart_err_t;

/**
 * @brief 接收数据回调函数类型
 * @param  data  接收到的数据指针
 * @param  len   数据长度（字节）
 * @note   回调在轮询上下文中执行，应尽快返回
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
 * @return BT_UART_OK 成功，其他为错误码
 */
bt_uart_err_t bt_uart_service_init(void);

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
 * @note   数据直接通过 BluetoothSerial 发送。
 *         单次调用最大可发送 BT_UART_TX_BUF_SIZE 字节。
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

/**
 * @brief  轮询蓝牙串口：读取数据并检测连接状态变化
 * @note   由调用者定期调用（如每帧一次）。
 *         RX 数据放入内部队列，需配合 bt_uart_drain_rx_queue() 消费。
 */
void bt_uart_poll(void);

/**
 * @brief  消费 RX 队列并调用回调
 * @note   应在 UI 任务中调用，避免跨任务写缓冲区。
 */
void bt_uart_drain_rx_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_UART_SERVICE_H */
