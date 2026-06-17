/**
 * @file   kern_task_stack.c
 * @brief  Xeros 任务栈管理实现
 * @details 栈分配/初始化、金丝雀写入、栈使用量查询。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched.h"
#include "kern_task.h"
#include "kern_kmalloc.h"
#include "kern_init.h"
#include "kern_port.h"

#include <string.h>
#include <stdlib.h>

/* ═══ 栈初始化 ═══ */

#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)

void task_stack_init(kern_task_t *task, size_t stack_size)
{
    if (stack_size > KERN_STACK_MAX) stack_size = KERN_STACK_MAX;
    if (stack_size < KERN_STACK_MIN) stack_size = KERN_STACK_MIN;

    task->stack_size = stack_size;
    task->stack_base = (uint8_t *)kern_kmalloc_for_task(task, stack_size);
    if (task->stack_base == NULL) {
        kern_log(KERN_LOG_WARN, "stack alloc failed for task %s, size=%zu",
                 task->name, stack_size);
        return;
    }

    memset(task->stack_base, 0xAA, stack_size);
}

void task_write_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return;
    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary = KERN_STACK_CANARY;
        memcpy(task->stack_base, &canary, sizeof(uint32_t));
    }
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器) ═══════════════ */

void task_stack_init(kern_task_t *task, size_t stack_size)
{
    (void)task;
    (void)stack_size;
    /* FreeRTOS 分配和管理任务栈 */
}

void task_write_canary(kern_task_t *task)
{
    (void)task;
    /* FreeRTOS 有自己的栈溢出检测 */
}

#endif

/* ═══ 栈使用量查询 ═══ */

#ifdef NATIVE_TEST

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;

    #define CANARY_SKIP 8
    size_t scan_start = (task->stack_size > CANARY_SKIP) ?
                        (size_t)CANARY_SKIP : task->stack_size;
    size_t used = 0;
    for (size_t i = scan_start; i < task->stack_size; i++) {
        if (task->stack_base[i] != 0xAA) {
            used = task->stack_size - i;
            break;
        }
    }

    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary;
        memcpy(&canary, task->stack_base, sizeof(uint32_t));
        if (canary != KERN_STACK_CANARY) {
            kern_log(KERN_LOG_WARN, "stack canary corrupted for task %s (pid=%d)",
                     task->name, task->pid);
        }
    }

    return (used > 0) ? used : 1;
}

#elif defined(XEROS_NATIVE_SCHED)

/* XEROS_NATIVE_SCHED: 手动栈 + 0xAA canary，与 NATIVE_TEST 相同 */

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;

    #define CANARY_SKIP 8
    size_t scan_start = (task->stack_size > CANARY_SKIP) ?
                        (size_t)CANARY_SKIP : task->stack_size;
    size_t used = 0;
    for (size_t i = scan_start; i < task->stack_size; i++) {
        if (task->stack_base[i] != 0xAA) {
            used = task->stack_size - i;
            break;
        }
    }

    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary;
        memcpy(&canary, task->stack_base, sizeof(uint32_t));
        if (canary != KERN_STACK_CANARY) {
            kern_log(KERN_LOG_WARN, "stack canary corrupted for task %s (pid=%d)",
                     task->name, task->pid);
        }
    }

    return (used > 0) ? used : 1;
}

#else /* ESP32: FreeRTOS 管理栈，通过 uxTaskGetStackHighWaterMark 查询 */

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL) return 0;
    /* 通过可移植层查询剩余栈字数，转换为已使用字节 */
    if (task->port_thread != KERN_PORT_THREAD_NULL) {
        size_t free_words = kern_port_thread_stack_usage(task->port_thread);
        /* ESP32 上 sizeof(StackType_t) == 4 */
        size_t free_bytes = free_words * 4;
        return (task->stack_size > free_bytes)
               ? (task->stack_size - free_bytes)
               : 0;
    }
    return 0;
}

#endif
