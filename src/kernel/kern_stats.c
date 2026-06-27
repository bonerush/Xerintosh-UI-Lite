#include "kern_stats.h"
#include "kern_sched.h"
#include "kern_task.h"
#include "kern_init.h"

#ifndef NATIVE_TEST
#include <esp_timer.h>
#endif

#include <string.h>

static uint64_t s_total_runtime_us = 0;
static uint64_t s_last_update_us = 0;

static uint64_t stats_time_us(void)
{
#ifdef NATIVE_TEST
    return g_sched_ticks * 1000ULL;
#else
    return (uint64_t)esp_timer_get_time();
#endif
}

void kern_stats_init(void)
{
    s_total_runtime_us = 0;
    s_last_update_us = stats_time_us();
}

void kern_stats_task_start(kern_task_t *task)
{
    if (task == NULL) return;
    task->last_start_us = stats_time_us();
}

void kern_stats_task_stop(kern_task_t *task)
{
    if (task == NULL || task->last_start_us == 0) return;
    uint64_t now = stats_time_us();
    task->runtime_us += now - task->last_start_us;
    task->last_start_us = 0;
}

void kern_stats_update(void)
{
    uint64_t now = stats_time_us();
    if (now - s_last_update_us < 100000) return;

    s_last_update_us = now;
    uint64_t total = 0;

    kern_task_t *t = g_task_list;
    while (t != NULL) {
        uint64_t runtime = t->runtime_us;
        if (t->state == KERN_TASK_RUNNING && t->last_start_us != 0) {
            runtime += now - t->last_start_us;
        }
        total += runtime;
        t->cpu_percent = 0;
        t = t->next;
    }

    if (total == 0) return;

    t = g_task_list;
    while (t != NULL) {
        uint64_t runtime = t->runtime_us;
        if (t->state == KERN_TASK_RUNNING && t->last_start_us != 0) {
            runtime += now - t->last_start_us;
        }
        t->cpu_percent = (uint8_t)((runtime * 100) / total);
        t = t->next;
    }
}

uint64_t kern_stats_get_runtime_us(const kern_task_t *task)
{
    if (task == NULL) return 0;
    return task->runtime_us;
}

uint8_t kern_stats_get_cpu_percent(const kern_task_t *task)
{
    if (task == NULL) return 0;
    return task->cpu_percent;
}

static uint32_t s_wdog_last_feed[KERN_MAX_TASKS] = {0};

kern_err_t kern_watchdog_register(kern_task_t *task)
{
    if (task == NULL || task->pid < 0 || task->pid >= KERN_MAX_TASKS)
        return KERN_EINVAL;
    s_wdog_last_feed[task->pid] = g_sched_ticks;
    return KERN_OK;
}

kern_err_t kern_watchdog_feed(kern_task_t *task)
{
    if (task == NULL || task->pid < 0 || task->pid >= KERN_MAX_TASKS)
        return KERN_EINVAL;
    s_wdog_last_feed[task->pid] = g_sched_ticks;
    return KERN_OK;
}

void kern_watchdog_check(void)
{
    uint32_t now = g_sched_ticks;
    for (int i = 0; i < KERN_MAX_TASKS; i++) {
        if (s_wdog_last_feed[i] == 0) continue;
        if ((now - s_wdog_last_feed[i]) > KERN_WATCHDOG_TIMEOUT_MS) {
            kern_log(KERN_LOG_ERROR, "watchdog: task %d timeout", i);
        }
    }
}
