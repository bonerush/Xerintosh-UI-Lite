/**
 * @file   serial_input.h
 * @brief  串口输入管理头文件
 * @details 提供通过串口接收 WiFi 密码和蓝牙配对码的状态机接口。
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
    SERIAL_STATE_WAITING_PAIR_CODE,    /* 等待输入蓝牙配对码 */
    SERIAL_STATE_PAIR_CODE_RECEIVED,   /* 配对码已接收 */
    SERIAL_STATE_CANCELLED             /* 输入已取消/超时 */
} serial_state_t;

/* ═══ 操作函数 ═══ */

/**
 * @brief 请求通过串口输入指定 SSID 的 WiFi 密码
 * @param ssid 目标 WiFi 的 SSID
 */
void           serial_request_wifi_password(const char *ssid);

/**
 * @brief 请求通过串口输入指定蓝牙设备的配对码
 * @param device_name 目标蓝牙设备名称
 */
void           serial_request_bt_pair_code(const char *device_name);

/**
 * @brief 请求通过串口输入指定蓝牙设备的配对码（含 MAC 地址）
 * @param device_name 目标蓝牙设备名称
 * @param device_addr 目标蓝牙设备 MAC 地址
 */
void           serial_request_bt_pair_code_with_addr(const char *device_name, const char *device_addr);

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
 * @brief  获取用户输入的字符串（密码或配对码）
 * @return 输入字符串指针；未就绪时返回 NULL
 * @note   首次调用后会将状态置为 IDLE，需及时保存结果
 */
const char*    serial_get_input(void);

/**
 * @brief  获取当前输入目标名称（SSID 或设备名）
 * @return 目标名称指针
 */
const char*    serial_get_target_name(void);

/**
 * @brief  获取当前输入目标 MAC 地址（蓝牙配对时使用）
 * @return MAC 地址指针；无地址时返回 NULL
 */
const char*    serial_get_target_addr(void);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_INPUT_H */
