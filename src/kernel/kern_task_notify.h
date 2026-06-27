/**
 * @file   kern_task_notify.h
 * @brief  Xeros 任务通知接口头文件
 * @details 定义任务通知的枚举、初始化及公共 API。
 *          任务通知是一种轻量级同步机制，用于任务间发送信号或传递 32 位值。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_TASK_NOTIFY_H
#define KERN_TASK_NOTIFY_H

#include "kern_types.h"
#include "kern_task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KERN_NOTIFY_NO_ACTION = 0,
    KERN_NOTIFY_SET_BITS,
    KERN_NOTIFY_INCREMENT,
    KERN_NOTIFY_SET_VALUE_WITH_OVERWRITE,
    KERN_NOTIFY_SET_VALUE_WITHOUT_OVERWRITE
} kern_notify_action_t;

kern_err_t kern_task_notify_init(void);
kern_err_t kern_task_notify_give(kern_task_t *task);
kern_err_t kern_task_notify(kern_task_t *task, uint32_t value,
                            kern_notify_action_t action);
bool       kern_task_notify_take(bool clear_on_exit, uint32_t timeout_ms);
uint32_t   kern_task_notify_wait_bits(uint32_t bits_to_wait, bool clear_on_exit,
                                       bool wait_all, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TASK_NOTIFY_H */
