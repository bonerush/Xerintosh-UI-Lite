/**
 * @file   app_mem.h
 * @brief  App 层统一内存视图接口
 * @details 包装内核内存统计接口，为 WiFi/BT 管理器等 App 提供
 *          场景自适应的内存检查与保留水位支持。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef APP_MEM_H
#define APP_MEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/kern_kmalloc.h"  /* kern_kmem_stat_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  获取系统内存统计（内核接口包装）
 */
bool xeros_mem_get_stats(kern_kmem_stat_t *out);

/**
 * @brief  获取扣除保留水位后的可用内存（字节）
 * @note   如果 free_bytes <= reserved，返回 0
 */
uint32_t xeros_mem_available_bytes(void);

/**
 * @brief  判断是否能安全分配指定内存
 * @param  needed_bytes      需要的总空闲字节
 * @param  needed_contiguous 需要的最大连续空闲字节
 * @return true  总空闲与最大连续块均满足（扣除保留水位后）
 */
bool xeros_mem_can_alloc(uint32_t needed_bytes, uint32_t needed_contiguous);

#ifdef __cplusplus
}
#endif

#endif /* APP_MEM_H */
