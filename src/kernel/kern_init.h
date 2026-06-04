/**
 * @file   kern_init.h
 * @brief  Xeros 内核初始化、日志与 panic 系统头文件
 * @details 提供内核启动入口、分级日志输出、致命错误 panic 处理。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_INIT_H
#define KERN_INIT_H

#include "kern_types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化 Xeros 内核
 * @note  初始化日志系统、VFS 根节点、调度器就绪队列
 */
extern void kern_init(void);

/* ═══ 日志系统 ═══ */

/**
 * @brief  设置当前日志级别（低于此级别的日志将被过滤）
 * @param  level 日志级别阈值
 */
extern void kern_log_set_level(kern_log_level_t level);

/**
 * @brief  获取当前日志级别
 * @return 当前日志级别
 */
extern kern_log_level_t kern_log_get_level(void);

/**
 * @brief  内核日志输出（格式化）
 * @param  level 日志级别
 * @param  fmt   格式化字符串（printf 风格）
 * @param  ...   可变参数
 * @note   低于当前日志级别阈值的消息将被静默丢弃
 */
extern void kern_log(kern_log_level_t level, const char *fmt, ...);

/**
 * @brief  内核日志输出（va_list 版本）
 * @param  level 日志级别
 * @param  fmt   格式化字符串
 * @param  args  可变参数列表
 */
extern void kern_vlog(kern_log_level_t level, const char *fmt, va_list args);

/* ═══ Panic ═══ */

/**
 * @brief  内核致命错误处理（不可恢复）
 * @param  msg 错误描述信息
 * @note   打印调用栈（如果可用）、任务信息后无限循环
 */
extern void kern_panic(const char *msg);

/**
 * @brief  清除 panic 状态
 * @note   用于测试重置
 */
extern void kern_clear_panic(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_INIT_H */
