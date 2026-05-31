/**
 * @file   ble_serial.h
 * @brief  BLE 串口监视器 App 头文件
 * @details 声明 BLE 串口监视器的生命周期函数，供 Xerintosh UI 菜单树注册使用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BLE_SERIAL_H
#define BLE_SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BLE 串口监视器初始化（user_item 回调）
 */
void ble_serial_init(void *ud);

/**
 * @brief BLE 串口监视器主循环（user_item 回调）
 */
void ble_serial_loop(void *ud);

/**
 * @brief BLE 串口监视器退出（user_item 回调）
 */
void ble_serial_exit(void *ud);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SERIAL_H */
