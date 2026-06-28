/**
 * @file   wifi_manager.h
 * @brief  WiFi 管理器头文件
 * @details 提供 WiFi 状态机、启用/禁用、扫描、连接及 UI 菜单动态构建接口。
 *
 *          使用模板请参阅 doc/tutorials/api-templates.md 模板 7。
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
 * @brief  查询是否正在等待串口输入密码
 * @return true  正在等待输入（CONNECTING 状态且尚未发起连接）
 * @note   自动重连路径（g_connecting 已置位）不视为等待输入，
 *         避免 app_input 错误取消自动重连流程。
 */
bool wifi_mgr_is_waiting_input(void);

/**
 * @brief 查询 WiFi 是否已启用
 * @return true  WiFi 驱动已初始化
 * @return false WiFi 未初始化
 */
bool wifi_mgr_is_enabled(void);

/**
 * @brief  查询 WiFi 驱动是否真实初始化成功
 */
bool wifi_mgr_is_driver_on(void);

/**
 * @brief  查询 WiFi 是否已成功连接到 AP 且已获取 IP 地址
 * @return true  WiFi 状态机处于 CONNECTED 状态且 netif 拥有有效 IP
 */
bool wifi_mgr_is_connected(void);

/**
 * @brief  确保 STA netif 上已配置 DNS 服务器
 * @note   若 DHCP 未提供 DNS，则设置 8.8.8.8 作为主 DNS 回退。
 *         应在发起网络请求前调用，无副作用（已有 DNS 则跳过）。
 */
void wifi_mgr_ensure_dns(void);

/**
 * @brief  返回 WiFi 启用建议的最小空闲内存（字节）
 */
uint32_t wifi_mgr_needed_heap(void);

/**
 * @brief 每帧更新 WiFi 状态机（非阻塞）
 * @note  应在主循环中每帧调用，处理扫描超时、连接超时、状态转换等
 */
void wifi_mgr_update(void);

/**
 * @brief 请求启用 WiFi（线程安全，可从任意任务调用）
 * @note  实际启用操作在 wifi_mgr_process_requests() 中执行，
 *        必须在主任务（app_main 调度循环）上下文中调用。
 */
void wifi_mgr_request_enable(void);

/**
 * @brief 请求禁用 WiFi（线程安全，可从任意任务调用）
 * @note  实际禁用操作在 wifi_mgr_process_requests() 中执行，
 *        必须在主任务（app_main 调度循环）上下文中调用。
 */
void wifi_mgr_request_disable(void);

/**
 * @brief 处理挂起的 WiFi 启用/禁用请求
 * @note  应在 app_main 的主循环中每帧调用，统一执行 WiFi 驱动操作。
 */
void wifi_mgr_process_requests(void);

/**
 * @brief WiFi 开关切换回调（由 switch_item 的 exit_function 调用）
 * @note  根据 g_wifi_on 全局变量决定启用或禁用
 */
void wifi_mgr_on_switch_toggle(void *ud);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
