/**
 * @file   wifi_manager.h
 * @brief  WiFi 管理器头文件
 * @details 提供 WiFi 状态机、启用/禁用、扫描、连接及 UI 菜单动态构建接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 类型定义 ═══ */

/**
 * @brief WiFi 管理器状态机枚举
 * @note  状态流转：
 *        IDLE → WARMUP → SCANNING → SCAN_DONE → CONNECTING → CONNECTED/CONNECT_FAILED
 */
typedef enum {
    WIFI_MGR_IDLE,           /* 空闲/关闭 */
    WIFI_MGR_WARMUP,         /* 预热中（等待 WiFi 驱动就绪） */
    WIFI_MGR_SCANNING,       /* 扫描中（异步） */
    WIFI_MGR_SCAN_DONE,      /* 扫描完成 */
    WIFI_MGR_CONNECTING,     /* 连接中（等待串口输入密码或正在连接） */
    WIFI_MGR_CONNECTED,      /* 已连接 */
    WIFI_MGR_CONNECT_FAILED  /* 连接失败 */
} wifi_mgr_state_t;

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 WiFi 管理器
 */
void wifi_mgr_init(void);

/* ═══ 操作函数 ═══ */

/**
 * @brief 启用 WiFi（进入 STA 模式并开始预热）
 */
void wifi_mgr_enable(void);

/**
 * @brief 禁用 WiFi（断开连接、关闭驱动、清理菜单）
 */
void wifi_mgr_disable(void);

/**
 * @brief  查询 WiFi 是否已启用
 * @return true  已启用
 * @return false 已禁用
 */
bool wifi_mgr_is_enabled(void);

/**
 * @brief  获取当前 WiFi 状态机状态
 * @return 当前状态
 */
wifi_mgr_state_t wifi_mgr_get_state(void);

/**
 * @brief  查询是否正在等待串口输入密码
 * @return true  等待输入中（CONNECTING 状态）
 */
bool wifi_mgr_is_waiting_input(void);

/**
 * @brief 每帧更新 WiFi 状态机（非阻塞）
 * @note  应在主循环中每帧调用，处理扫描超时、连接超时、状态转换等
 */
void wifi_mgr_update(void);

/**
 * @brief WiFi 开关切换回调（由 switch_item 的 exit_function 调用）
 * @note  根据 g_wifi_on 全局变量决定启用或禁用
 */
void wifi_mgr_on_switch_toggle(void *ud);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
