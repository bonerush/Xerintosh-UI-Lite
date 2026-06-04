/**
 * @file   kern_smp.c
 * @brief  Xeros SMP 多核支持实现
 * @details 定义 per-CPU 数组、CPU ID 查询、SMP 初始化。
 *          当 CONFIG_SMP_ENABLED 未定义时，此文件仅定义 g_per_cpu[0]
 *          （单核零开销模式）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_smp.h"
#include "kern_init.h"

/* ═══ Per-CPU 数组定义（总是存在） ═══ */

kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];

#ifdef CONFIG_SMP_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

volatile uint8_t g_active_cpu = 0;

/* ═══ CPU ID ═══ */

uint8_t kern_cpu_id(void)
{
    return (uint8_t)xPortGetCoreID();
}

/* ═══ SMP 初始化 ═══ */

void kern_smp_init(void)
{
    for (uint8_t i = 0; i < KERN_MAX_CPUS; i++) {
        g_per_cpu[i].cpu_id       = i;
        g_per_cpu[i].current_task = NULL;
        g_per_cpu[i].idle_task    = NULL;
        g_per_cpu[i].sched_ticks  = 0;
        g_per_cpu[i].need_resched = false;
        g_per_cpu[i].last_picked  = NULL;
        g_per_cpu[i].task_count   = 0;
    }
    kern_log(KERN_LOG_INFO, "SMP: %d CPUs initialized", KERN_MAX_CPUS);
}

/* ═══ 启动辅助核心 ═══ */

void kern_smp_start_core(uint8_t cpu_id, void (*entry)(void *arg))
{
    if (cpu_id >= KERN_MAX_CPUS) return;

    BaseType_t ret = xTaskCreatePinnedToCore(
        (TaskFunction_t)entry,
        "xeros_smp",
        4096,
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL,
        cpu_id
    );

    if (ret != pdPASS) {
        kern_log(KERN_LOG_WARN, "SMP: failed to start core %d scheduler", cpu_id);
    } else {
        kern_log(KERN_LOG_INFO, "SMP: core %d scheduler started", cpu_id);
    }
}

/* ═══ CPU 分配策略 ═══ */

uint8_t kern_smp_migrate_assign(void)
{
    /* 简单策略：分配到任务数较少的 CPU */
    if (g_per_cpu[0].task_count <= g_per_cpu[1].task_count) {
        g_per_cpu[0].task_count++;
        return 0;
    } else {
        g_per_cpu[1].task_count++;
        return 1;
    }
}

#endif /* CONFIG_SMP_ENABLED */
