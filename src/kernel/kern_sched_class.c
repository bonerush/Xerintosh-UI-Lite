/**
 * @file   kern_sched_class.c
 * @brief  Xeros 可插拔调度类注册与选择实现
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched_class.h"
#include "kern_sched.h"
#include <stddef.h>

/* ═══ 全局 class 注册表 ═══ */

kern_sched_class_t *g_sched_classes[KERN_SCHED_MAX_CLASSES] = { NULL };
uint8_t g_sched_class_count = 0;

void kern_sched_class_register(kern_sched_class_t *cls)
{
    if (cls == NULL || g_sched_class_count >= KERN_SCHED_MAX_CLASSES) return;
    g_sched_classes[g_sched_class_count++] = cls;
}

struct kern_task *pick_next_ready(void)
{
    for (uint8_t i = 0; i < g_sched_class_count; i++) {
        kern_sched_class_t *cls = g_sched_classes[i];
        if (cls == NULL || cls->pick_next == NULL) continue;
        struct kern_task *task = cls->pick_next();
        if (task != NULL) return task;
    }
    return NULL;
}
