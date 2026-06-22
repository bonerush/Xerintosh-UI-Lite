/**
 * @file   serial_monitor.h
 * @brief  串口监视器 App 头文件
 * @details 声明串口监视器的生命周期函数，供 Xerintosh UI 菜单树注册使用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SERIAL_MONITOR_H
#define SERIAL_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期（user_item 接口）═══ */

/**
 * @brief 初始化串口监视器 App
 * @note  重置状态、清空缓冲区
 */
void serial_monitor_init(void *ud);

/**
 * @brief 串口监视器主循环（每帧调用）
 * @note  处理输入事件、更新闪烁、绘制界面
 */
void serial_monitor_loop(void *ud);

/**
 * @brief 退出串口监视器 App
 * @note  若处于 NORM 模式则清空缓冲区；DEBUG 模式保留历史
 */
void serial_monitor_exit(void *ud);

/* ═══ 后台更新 ═══ */

/**
 * @brief 后台串口数据读取（每帧由 main.cpp loop 调用）
 * @note  仅在 START 或 DEBUG 状态下读取串口数据并缓存到环形缓冲区。
 *        若 serial_input 处于 WAITING 状态（等待密码/配对码），暂停读取避免竞争。
 */
void serial_monitor_update(void);

/**
 * @brief  查询串口监视器是否活跃（运行中或 DEBUG 模式）
 * @return true  监视器正在消费串口，dev_ttyS0_poll 应暂停 RX
 */
bool serial_monitor_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_MONITOR_H */
