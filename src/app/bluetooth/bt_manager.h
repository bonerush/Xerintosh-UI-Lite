/**
 * @file   bt_manager.h
 * @brief  蓝牙管理器头文件
 * @details 提供蓝牙状态机、启用/禁用、扫描、配对及 UI 菜单动态构建接口。
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
 *        IDLE → WARMUP → SCANNING → SCAN_DONE → PAIRING → PAIRED/PAIR_FAILED
 */
typedef enum {
    BT_MGR_IDLE,        /* 空闲/关闭 */
    BT_MGR_WARMUP,      /* 预热中 */
    BT_MGR_SCANNING,    /* 扫描中 */
    BT_MGR_SCAN_DONE,   /* 扫描完成 */
    BT_MGR_PAIRING,     /* 等待串口输入配对码 */
    BT_MGR_PAIRED,      /* 已配对 */
    BT_MGR_PAIR_FAILED  /* 配对失败 */
} bt_mgr_state_t;

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化蓝牙管理器
 */
void bt_mgr_init(void);

/* ═══ 操作函数 ═══ */

/**
 * @brief 启用蓝牙（初始化 NimBLE 并开始预热）
 */
void bt_mgr_enable(void);

/**
 * @brief 禁用蓝牙（释放 NimBLE、清理菜单）
 */
void bt_mgr_disable(void);

/**
 * @brief  查询蓝牙是否已启用
 * @return true  已启用
 * @return false 已禁用
 */
bool bt_mgr_is_enabled(void);

/**
 * @brief  获取当前蓝牙状态机状态
 * @return 当前状态
 */
bt_mgr_state_t bt_mgr_get_state(void);

/**
 * @brief  查询是否正在等待串口输入配对码
 * @return true  等待输入中（PAIRING 状态）
 */
bool bt_mgr_is_waiting_input(void);

/**
 * @brief 每帧更新蓝牙状态机（非阻塞）
 * @note  应在主循环中每帧调用，处理扫描超时、配对输入等
 */
void bt_mgr_update(void);

/**
 * @brief 蓝牙开关切换回调（由 switch_item 的 exit_function 调用）
 * @note  根据 bt_on 全局变量决定启用或禁用
 */
void bt_mgr_on_switch_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_MANAGER_H */
