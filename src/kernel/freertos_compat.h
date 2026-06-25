/**
 * @file   freertos_compat.h
 * @brief  FreeRTOS API 兼容层
 * @details 始终包含 FreeRTOS 头文件（ESP-IDF 组件依赖它们的类型定义）。
 *          当 XEROS_NATIVE_SCHED 启用时，额外将关键 FreeRTOS API
 *          重定向到 Xeros 内核原语，避免在原生调度器上下文中执行
 *          FreeRTOS 的 yield/delay 操作。
 *
 * 用法：
 *   在需要 FreeRTOS API 的文件中，替换直接的 FreeRTOS 头文件包含：
 *   #include "kernel/freertos_compat.h"
 *
 * @copyright Copyright (c) 2026
 */

#ifndef FREERTOS_COMPAT_H
#define FREERTOS_COMPAT_H

/* ═══ 始终包含 FreeRTOS 头文件（提供类型定义和 ESP-IDF 传递依赖） ═══ */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#if defined(XEROS_NATIVE_SCHED)

/* ═══════════════════════════════════════════════════════════════════════════
 *  原生调度器模式：重定向关键 FreeRTOS API 到 Xeros 内核原语
 *
 *  仅覆盖任务延迟和删除——这两个操作在原生调度器下必须走 Xeros 的
 *  上下文切换路径，而不是 FreeRTOS 的 vTaskDelay。
 *
 *  portMUX / portENTER_CRITICAL / 信号量保持 FreeRTOS 原生实现：
 *  在 XEROS_NATIVE_SCHED 下，调度器循环运行在 FreeRTOS 任务中，
 *  FreeRTOS 的临界区原语仍然有效。
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "kern_types.h"
#include "kern_task.h"

/* ── 任务延迟 ──
 * vTaskDelay 会 yield 到 FreeRTOS 调度器，但在原生调度器下
 * 我们需要通过 kern_sleep_ms 走 Xeros 上下文切换路径。 */

#undef vTaskDelay
#define vTaskDelay(ticks)       kern_sleep_ms((uint32_t)(ticks))

/* ── 任务删除 ── */

#undef vTaskDelete
#define vTaskDelete(handle)     kern_exit()

#endif /* XEROS_NATIVE_SCHED */

#endif /* FREERTOS_COMPAT_H */
