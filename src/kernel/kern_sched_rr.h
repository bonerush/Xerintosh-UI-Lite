/**
 * @file   kern_sched_rr.h
 * @brief  Round-Robin 调度器类头文件
 * @details 声明 sched_class_rr 全局实例，作为默认时间片轮转调度类。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_SCHED_RR_H
#define KERN_SCHED_RR_H

#include "kern_sched_class.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Round-Robin 调度器类实例 */
extern kern_sched_class_t sched_class_rr;

/** 默认时间片（tick 数） */
#define SCHED_RR_DEFAULT_TIMESLICE 10

/** 高内存压力下的时间片（tick 数） */
#define SCHED_RR_HIGH_PRESSURE_TIMESLICE 3

#ifdef __cplusplus
}
#endif

#endif /* KERN_SCHED_RR_H */
