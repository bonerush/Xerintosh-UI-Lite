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

kern_err_t kern_sched_class_register(kern_sched_class_t *cls)
{
    if (cls == NULL) {
        return KERN_EINVAL;
    }
    if (g_sched_class_count >= KERN_SCHED_MAX_CLASSES) {
        return KERN_ENOSPC;
    }
    cls->class_id = (int8_t)g_sched_class_count;
    g_sched_classes[g_sched_class_count++] = cls;
    return KERN_OK;
}

struct kern_task *pick_next_ready(void)
{
    /* 类按注册顺序获得 class_id，但调度优先级与注册顺序相反：
     * 后注册的类（如 FIFO）具有更高调度优先级，先被查询；
     * 先注册的 RR 作为兜底。这样 KERN_SCHED_CLASS_RR_ID 保持为 0
     * 与新任务默认加入 RR 的语义不变，同时 FIFO 任务能真正被优先选中。 */
    for (int i = (int)g_sched_class_count - 1; i >= 0; i--) {
        kern_sched_class_t *cls = g_sched_classes[i];
        if (cls == NULL || cls->pick_next == NULL) continue;
        struct kern_task *task = cls->pick_next();
        if (task != NULL) return task;
    }
    return NULL;
}
