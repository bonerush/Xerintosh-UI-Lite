/**
 * @file   bt_manager.h
 * @brief  蓝牙管理器头文件（Classic Bluetooth SPP）
 * @details 提供蓝牙状态机、启用/禁用及 UI 菜单接口。
 *          基于 Classic Bluetooth SPP，BLE 扫描/配对功能已移除。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BT_MANAGER_H
#define BT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 类型定义 ═══ */

/**
 * @brief 蓝牙管理器状态机枚举
 * @note  状态流转：
 *        IDLE → WARMUP → ENABLED → CONNECTED
 */
typedef enum {
    BT_MGR_IDLE,       /* 空闲/关闭 */
    BT_MGR_WARMUP,     /* 预热中（启动 BluetoothSerial） */
    BT_MGR_ENABLED,    /* 已启用，等待连接 */
    BT_MGR_CONNECTED,  /* 有客户端连接 */
} bt_mgr_state_t;

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化蓝牙管理器
 */
void bt_mgr_init(void);

/* ═══ 操作函数 ═══ */

/**
 * @brief 启用蓝牙（启动 BluetoothSerial 并开始预热）
 */
void bt_mgr_enable(void);

/**
 * @brief 禁用蓝牙（释放 BluetoothSerial）
 */
void bt_mgr_disable(void);

/**
 * @brief  查询是否正在等待串口输入配对码
 * @return true  等待输入中（PAIRING 状态）
 * @note   Classic BT SPP 不需要串口输入配对码，始终返回 false
 */
bool bt_mgr_is_waiting_input(void);

/**
 * @brief 每帧更新蓝牙状态机（非阻塞）
 * @note  应在主循环中每帧调用，处理预热、连接状态检测等
 */
void bt_mgr_update(void);

/**
 * @brief 查询蓝牙是否已启用
 * @return true  蓝牙驱动已初始化
 * @return false 蓝牙未初始化
 */
bool bt_mgr_is_enabled(void);

/**
 * @brief 蓝牙开关切换回调（由 switch_item 的 exit_function 调用）
 * @note  根据 g_bt_on 全局变量决定启用或禁用
 */
void bt_mgr_on_switch_toggle(void *ud);

#ifdef __cplusplus
}
#endif

#endif /* BT_MANAGER_H */
