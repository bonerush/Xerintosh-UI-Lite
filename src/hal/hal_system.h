/**
 * @file   hal_system.h
 * @brief  HAL 系统层头文件
 * @details 提供系统级基础服务：初始化、毫秒级时间获取、延时。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化系统子系统（时间基准等）
 */
extern void hal_system_init(void);

/* ═══ 操作函数 ═══ */

/**
 * @brief  获取系统启动后的单调毫秒数
 * @return 毫秒时间戳
 */
extern uint32_t hal_get_ticks_ms(void);

/**
 * @brief  hal_get_ticks_ms() 的兼容别名
 * @deprecated 新代码请使用 hal_get_ticks_ms()
 */
static inline uint32_t hal_get_ticks(void) { return hal_get_ticks_ms(); }

/**
 * @brief  延时指定的毫秒数
 * @param  ms 延时时间（毫秒）
 */
extern void hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYSTEM_H */
