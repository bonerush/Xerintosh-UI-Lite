/* Only compile for Xtensa targets (ESP32) with native scheduler enabled.
 * Skip on native test builds (ucontext). */
#if !defined(NATIVE_TEST) && defined(XEROS_NATIVE_SCHED)

/**
 * @file   kern_port_esp32_native.c
 * @brief  Xeros 内核可移植层 — ESP32 原生调度器后端
 * @details Xeros 原生调度器（复用 newlib setjmp/longjmp）：
 *          - 调度器运行在 app_main 的 ESP-IDF 任务上下文中。
 *          - Xeros 任务使用独立的私有栈；调度器与任务通过 jmp_buf 协切换。
 *          - 新任务第一次运行时走 xeros_task_start 汇编桩切换到任务栈。
 *          - tickless：idle 中按下一个 SLEEPING 任务的唤醒时间重编程 GPTimer，
 *            用低功耗忙等替代固定 1ms tick；无法进入 tickless 时回退到 ROM 延时。
 *
 *          本文件通过 kern_port_ops_t 提供后端多态。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_port.h"
#include "kern_task.h"
#include "kern_sched.h"
#include "kern_init.h"
#include "kernel/debug_serial.h"
#include "esp32/ctx_switch.h"
#include "esp32/tick_timer.h"

#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

#include "esp32/rom/ets_sys.h"
#include "esp_timer.h"

/* 原生调度器调试输出开关：0=关闭大量切换日志，1=打开 */
#define NATIVE_SCHED_DEBUG 0
#if NATIVE_SCHED_DEBUG
#define NATIVE_LOG(fmt, ...) debug_printf("[native] " fmt, ##__VA_ARGS__)
#else
#define NATIVE_LOG(fmt, ...) do { } while (0)
#endif

/* 调度器上下文定义在 kern_sched.c（per-CPU） */
extern kern_ctx_native_t g_sched_ctx[KERN_MAX_CPUS];

/* 当前 CPU 的调度器上下文快捷访问 */
#define sched_ctx_current() (&g_sched_ctx[kern_cpu_id()])

/* ═══ 生命周期 ═══ */

static void native_init(void)
{
    /* 原生调度器不需要信号量；定时器按需启动 */
}

/* ═══ 线程管理（原生调度器自行管理任务上下文）═══ */

static kern_port_thread_t native_thread_spawn(
    void (*entry)(void *arg),
    void *arg,
    const char *name,
    size_t stack_size,
    kern_task_t *task)
{
    (void)entry;
    (void)arg;
    (void)name;
    (void)stack_size;
    (void)task;
    /* 任务栈与上下文由 kern_spawn / task_stack_init 直接建立 */
    return KERN_PORT_THREAD_NULL;
}

static void native_thread_exit(void)
{
    /* 不应被直接调用；任务退出通过 native_task_exit 恢复调度器上下文 */
    kern_log(KERN_LOG_ERROR, "native_thread_exit: unexpected call");
    while (1) {
        ets_delay_us(1000000);
    }
}

static void native_thread_kill(kern_port_thread_t thread)
{
    (void)thread;
    /* 原生调度器没有底层线程句柄；外部终止在 kern_task_kill 中标记 ZOMBIE */
}

static size_t native_thread_stack_usage(kern_port_thread_t thread)
{
    (void)thread;
    /* 原生栈使用量由 kern_task_stack_usage() 直接扫描 canary 计算 */
    return 0;
}

/* ═══ 上下文切换 ═══ */

static void native_switch_to(kern_task_t *task)
{
    if (task == NULL) return;

    NATIVE_LOG("switch_to: %s (state=%d)\n", task->name, task->state);

    /* setjmp 语义：保存调度器上下文后首次返回 0，切换到目标任务；
     * 当任务 yield/exit 时通过 longjmp 返回，此时返回 1。 */
    int ret = xeros_ctx_save(sched_ctx_current());
    NATIVE_LOG("switch_to save ret=%d\n", ret);
    if (ret == 0) {
        if (task->native_ctx_valid) {
            /* 任务已有 setjmp 建立的上下文，直接恢复 */
            xeros_ctx_restore(task->native_ctx);
        } else {
            /* 新任务第一次运行：切换 SP 到任务栈并进入包装函数 */
            xeros_task_start(task);
        }
    }
    NATIVE_LOG("switch_to resumed\n");

    /* 任务至少运行过一次（yield 或 exit），其 jmp_buf 已可用 */
    task->native_ctx_valid = true;
}

static void native_task_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    NATIVE_LOG("task_yield: saving %s\n", cur->name);
    if (xeros_ctx_save(cur->native_ctx) == 0) {
        NATIVE_LOG("task_yield: save done, restoring sched\n");
        xeros_ctx_restore(sched_ctx_current());
    }
    NATIVE_LOG("task_yield: resumed %s\n", cur->name);
    /* 恢复后继续执行 */
}

static void native_task_exit(void)
{
    /* 直接恢复当前 CPU 的调度器上下文；TCB 已在 kern_exit() 中标记为 ZOMBIE */
    xeros_ctx_restore(sched_ctx_current());
}

/* ═══ 空闲处理 ═══ */

/* 进入 tickless 的最小空闲时长（毫秒）
 * 小于此值时保持 1ms 周期 tick，避免频繁重编程定时器。 */
#define TICKLESS_MIN_IDLE_MS 1

static uint32_t native_idle_next_wake_ms(void)
{
    uint32_t now = g_sched_ticks;
    uint32_t next = 0;

#ifdef CONFIG_SMP_ENABLED
    /* 全局任务链表可能被其他核心并发修改（spawn/kill/reap），
     * idle 计算下一唤醒时间时必须持锁读取。 */
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        __asm__ volatile("nop");
    }
#endif

    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time > now) {
            if (next == 0 || t->wake_time < next) {
                next = t->wake_time;
            }
        }
        t = t->next;
    }

#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif

    return next;
}

static void native_idle(void)
{
    /* 只有 Core 0 控制全局 GPTimer 的 tickless 重编程；
     * Core 1 直接 ROM 延时，避免两个核互相覆盖 alarm 导致对方 sleep 任务延迟唤醒。 */
    if (kern_cpu_id() != 0) {
        /* 将 1ms 拆成 10 段，便于被 IPI 设置的 g_need_resched 快速打断 */
        for (int i = 0; i < 10 && !g_need_resched; i++) {
            ets_delay_us(100);
        }
        return;
    }

    /* 只有 idle 任务本身会调用此函数。
     * 如果有其他 SLEEPING 任务在更久之后才会唤醒，则进入 tickless。 */
    if (g_current_task == g_idle_task) {
        uint32_t next_wake = native_idle_next_wake_ms();
        if (next_wake != 0) {
            uint32_t idle_ms = next_wake - g_sched_ticks;
            if (idle_ms >= TICKLESS_MIN_IDLE_MS) {
                uint32_t idle_us = idle_ms * 1000;
                /* 限制最大睡眠时长，避免长时间无 tick 导致诊断失效 */
                if (idle_us > 100000) idle_us = 100000;  /* max 100ms */

                uint64_t t0 = esp_timer_get_time();
                tick_timer_set_next_alarm(idle_us);
                /* Phase 2 保守方案：先不执行 waiti 0（user mode 下可能触发异常），
                 * 用低功耗忙等直到下一次 tick 中断。后续再替换为真正的睡眠指令。 */
                while (!tick_timer_pending(kern_cpu_id()) && !g_need_resched) {
                    __asm__ volatile ("nop");
                }
                tick_timer_restore_periodic();

                /* 消费导致退出 tickless 的 tick 标志，避免调度器循环中
                 * kern_port_preempt_consume() 再次消费同一标志导致 g_sched_ticks
                 * 重复递增（补偿 +elapsed_ms 后又 +1）。 */
                tick_timer_consume(kern_cpu_id());

                /* 关键修复：tickless 期间 g_sched_ticks 未推进，但真实时间已流逝。
                 * 不补偿会导致睡眠任务的 wake_time 检查失效，任务实际等待时间
                 * 远超预期，长时间空闲后 UI 卡死。 */
                uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
                if (elapsed_ms > 0) {
                    g_sched_ticks += elapsed_ms;
                }
                return;
            }
        }
    }

    /* 无法进入 tickless：等待下一个定时器 tick（最多 1ms）。
     * 使用定时器同步等待替代固定 ets_delay_us(200)，防止调度器循环以
     * ~5kHz 频率旋转导致 g_sched_ticks 以 5 倍速度递增。
     * g_sched_ticks 现在仅由调度器循环在 kern_port_preempt_consume()
     * 返回 true 时递增，确保 1 tick = 1ms 的语义一致性。 */
    while (!tick_timer_pending(kern_cpu_id()) && !g_need_resched) {
        __asm__ volatile ("nop");
    }
}

/* ═══ 定时器基础设施 ═══ */

#ifdef CONFIG_PREEMPT_ENABLED

static kern_err_t native_timer_set(uint32_t period_us)
{
    if (period_us == 0) return KERN_EINVAL;
    if (tick_timer_is_running()) {
        return KERN_OK;
    }
    if (tick_timer_init(period_us) != 0) {
        return KERN_ERR;
    }
    if (tick_timer_start() != 0) {
        tick_timer_stop();
        return KERN_ERR;
    }
    return KERN_OK;
}

static void native_timer_stop(void)
{
    tick_timer_stop();
}

static bool native_preempt_consume(void)
{
    return tick_timer_consume(kern_cpu_id());
}

#else /* !CONFIG_PREEMPT_ENABLED */

static kern_err_t native_timer_set(uint32_t period_us)
{
    (void)period_us;
    return KERN_OK;
}

static void native_timer_stop(void) {}

static bool native_preempt_consume(void)
{
    return false;
}

#endif /* CONFIG_PREEMPT_ENABLED */

/* ═══ 全局操作表 ═══ */

const kern_port_ops_t g_kern_port_ops = {
    .init                = native_init,
    .thread_spawn        = native_thread_spawn,
    .thread_exit         = native_thread_exit,
    .thread_kill         = native_thread_kill,
    .thread_stack_usage  = native_thread_stack_usage,
    .switch_to           = native_switch_to,
    .task_yield          = native_task_yield,
    .task_exit           = native_task_exit,
    .idle                = native_idle,
    .timer_set_periodic  = native_timer_set,
    .timer_stop          = native_timer_stop,
    .preempt_consume     = native_preempt_consume,
};

#endif /* !NATIVE_TEST && XEROS_NATIVE_SCHED */
