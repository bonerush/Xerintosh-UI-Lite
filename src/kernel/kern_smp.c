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
#include <esp_cpu.h>
#include <esp_ipc_isr.h>

volatile uint8_t g_cpu_ready = 0;  /* SMP 就绪标志（供外部查询） */

/* IPI 汇编处理函数（定义在 smp_ipi.S） */
extern void xeros_ipi_reschedule_handler(void *arg);

/* ESP32 为双核，确保 KERN_MAX_CPUS 至少为 2 */
_Static_assert(KERN_MAX_CPUS >= 2, "KERN_MAX_CPUS must be at least 2 for ESP32 SMP");

/* ═══ CPU ID ═══ */

uint8_t kern_cpu_id(void)
{
    /* 原生 CPU ID 读取：不依赖 FreeRTOS xPortGetCoreID。
     * esp_cpu_get_core_id() 直接读 Xtensa PRID 特殊寄存器。 */
    int id = esp_cpu_get_core_id();
    if (id < 0 || id >= KERN_MAX_CPUS) {
        kern_log(KERN_LOG_ERROR, "SMP: invalid core id %d >= KERN_MAX_CPUS", id);
        return 0;
    }
    return (uint8_t)id;
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

#ifdef CONFIG_SMP_ENABLED
/* 保护 g_per_cpu[].task_count 的负载均衡计数器 */
static volatile bool g_smp_assign_lock = false;
#endif

uint8_t kern_smp_migrate_assign(void)
{
#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_smp_assign_lock, true)) {
        __asm__ volatile("nop");
    }
#endif

    /* 简单策略：分配到任务数较少的 CPU */
    uint8_t cpu;
    if (g_per_cpu[0].task_count <= g_per_cpu[1].task_count) {
        g_per_cpu[0].task_count++;
        cpu = 0;
    } else {
        g_per_cpu[1].task_count++;
        cpu = 1;
    }

#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_smp_assign_lock);
#endif

    return cpu;
}

/* ═══ IPI（处理器间中断）═══ */

void kern_smp_ipi_reschedule(uint8_t cpu_id)
{
#ifdef CONFIG_SMP_ENABLED
    uint8_t self = kern_cpu_id();
    if (cpu_id >= KERN_MAX_CPUS || cpu_id == self) {
        return;
    }

    /* 目标核心尚未进入调度循环，无需/不能发送 IPI */
    if ((g_cpu_ready & (uint8_t)(1u << cpu_id)) == 0) {
        return;
    }

    /* ESP32 双核：esp_ipc_isr_call() 只能发往“另一核”。
     * 若请求的核心确实是对侧核心，则触发高优先级中断使其退出 idle。 */
    if (cpu_id == (self ^ 1)) {
        esp_ipc_isr_call(xeros_ipi_reschedule_handler,
                         (void *)&g_per_cpu[cpu_id].need_resched);
    }
#endif
}

#endif /* CONFIG_SMP_ENABLED */
