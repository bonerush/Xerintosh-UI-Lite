/**
 * @file tick_timer.h
 * @brief ESP32 硬件定时器抽象 — 用于抢占式调度 tick
 *
 * 基于 ESP32 Timer Group 0, Timer 0 (TGMT0) 实现周期性 tick 中断。
 * ISR 仅设置标志，调度逻辑在任务上下文中执行。
 *
 * @note 此模块独立于 FreeRTOS，可被原生调度器后端直接使用。
 */

#ifndef XERINTOSH_KERNEL_ESP32_TICK_TIMER_H
#define XERINTOSH_KERNEL_ESP32_TICK_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 tick 定时器
 *
 * 配置 TGMT0 为周期性定时器，注册 ISR 回调。
 * 定时器初始化后处于停止状态，需调用 tick_timer_start() 启动。
 *
 * @param period_us  tick 周期（微秒），典型值 1000（1ms）
 * @return 0 成功，< 0 失败
 */
int tick_timer_init(uint32_t period_us);

/**
 * @brief 启动 tick 定时器
 * @return 0 成功，< 0 失败
 */
int tick_timer_start(void);

/**
 * @brief 停止 tick 定时器
 */
void tick_timer_stop(void);

/**
 * @brief 检查并消费 tick 请求（任务上下文调用）
 *
 * 当 ISR 设置了 tick 标志时返回 true，同时清零标志。
 * 调度器在主循环中调用此函数来触发 kern_sched_tick()。
 *
 * @return true 如果有 tick 待处理
 */
bool tick_timer_consume(void);

/**
 * @brief 检查定时器是否正在运行
 * @return true 如果定时器已启动
 */
bool tick_timer_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* XERINTOSH_KERNEL_ESP32_TICK_TIMER_H */
