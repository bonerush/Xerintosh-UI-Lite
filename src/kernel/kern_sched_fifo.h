/**
 * @file   kern_sched_fifo.h
 * @brief  优先级 FIFO 调度器类头文件
 * @details 声明 sched_class_fifo 全局实例，提供基于优先级的抢占式调度。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SCHED_FIFO_H
#define KERN_SCHED_FIFO_H

#include "kern_sched_class.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 优先级 FIFO 调度器类实例 */
extern kern_sched_class_t sched_class_fifo;

#ifdef __cplusplus
}
#endif

#endif /* KERN_SCHED_FIFO_H */
