/**
 * @file   serial_input.h
 * @brief  串口输入管理头文件
 * @details 提供通过串口接收 WiFi 密码的状态机接口。
 *          支持非阻塞轮询、超时自动取消、退格编辑等功能。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SERIAL_INPUT_H
#define SERIAL_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 类型定义 ═══ */

/**
 * @brief 串口输入状态机枚举
 */
typedef enum {
    SERIAL_STATE_IDLE,                 /* 空闲 */
    SERIAL_STATE_WAITING_PASSWORD,     /* 等待输入 WiFi 密码 */
    SERIAL_STATE_PASSWORD_RECEIVED,    /* WiFi 密码已接收 */
    SERIAL_STATE_CANCELLED             /* 输入已取消/超时 */
} serial_state_t;

/* ═══ 操作函数 ═══ */

/**
 * @brief 请求通过串口输入指定 SSID 的 WiFi 密码
 * @param ssid 目标 WiFi 的 SSID
 */
void           serial_request_wifi_password(const char *ssid);

/**
 * @brief 取消当前串口输入请求
 */
void           serial_cancel(void);

/**
 * @brief  轮询串口输入状态（非阻塞）
 * @return 当前状态
 */
serial_state_t serial_poll(void);

/**
 * @brief  获取用户输入的字符串（WiFi 密码）
 * @return 输入字符串指针；未就绪时返回 NULL
 * @note   首次调用后会将状态置为 IDLE，需及时保存结果
 */
const char*    serial_get_input(void);

/**
 * @brief  获取当前输入目标名称（SSID）
 * @return 目标名称指针
 */
const char*    serial_get_target_name(void);

/**
 * @brief  查询是否正在等待用户输入（WiFi 密码）
 * @return true  正在等待输入
 * @return false 空闲状态
 * @note   用于 dev_ttyS0_poll() 判断是否将串口字符留给 serial_input
 *         而非写入 /dev/ttyS0 ring buffer 供 shell 消费。
 */
bool           serial_input_is_waiting(void);

/**
 * @brief  UART0 共享自旋锁（serial_input 与 serial_monitor 互斥）
 * @note   用于防止两个任务同时消费硬件 UART 缓冲区字节。
 *         在 serial_poll() 和 serial_monitor_update() 中使用。
 */
extern volatile bool g_serial_uart_lock;

static inline void serial_uart_lock(void) {
    while (__sync_lock_test_and_set(&g_serial_uart_lock, true)) { asm volatile("nop"); }
}
static inline void serial_uart_unlock(void) {
    __sync_lock_release(&g_serial_uart_lock);
}

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_INPUT_H */
